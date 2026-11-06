"""Exposure-aware measurement geometry for independent reconstruction audits.

This module does not publish a live room, change calibration or contact a Frame.
Coordinates are metres in the observation's original tracking frame. Pixel
centres follow the reader's explicit point or area step-four sampling convention.
"""
from __future__ import annotations

from dataclasses import dataclass
import base64
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial import cKDTree

from .live_geometry import matrix, project_kb, rectify, row_poses, sampling_offset


def unproject_kb(pixels, intrinsics, coefficients):
    """Invert the factory KB model only on its monotonic forward branch."""
    pixels = np.asarray(pixels, dtype=np.float64).reshape(-1, 2)
    intrinsics = np.asarray(intrinsics, dtype=np.float64)
    k = np.asarray(coefficients, dtype=np.float64)
    xy = (pixels-intrinsics[2:])/intrinsics[:2]
    radius = np.linalg.norm(xy, axis=1)

    def distorted(theta):
        t = theta*theta
        return theta*(1+t*(k[0]+t*(k[1]+t*(k[2]+t*k[3]))))

    grid = np.linspace(0, np.pi/2-1e-5, 1024)
    t = grid*grid
    derivative = 1+t*(3*k[0]+t*(5*k[1]+t*(7*k[2]+t*9*k[3])))
    turning = np.flatnonzero(derivative <= 0)
    maximum = grid[max(0, int(turning[0])-1)] if len(turning) else grid[-1]
    valid = np.isfinite(xy).all(axis=1) & (radius <= distorted(maximum))
    lo, hi = np.zeros_like(radius), np.full_like(radius, maximum)
    # Bisection avoids jumping to another polynomial branch near a lens edge.
    for _ in range(48):
        middle = (lo+hi)/2
        lower = distorted(middle) < radius
        lo = np.where(lower, middle, lo)
        hi = np.where(lower, hi, middle)
    theta = (lo+hi)/2
    scale = np.sin(theta)/np.maximum(radius, 1e-15)
    rays = np.column_stack((xy*scale[:, None], np.cos(theta)))
    rays[radius < 1e-15] = (0, 0, 1)
    return rays, valid


@dataclass
class ObservationCamera:
    observation: dict
    eye: int
    frame: int

    def __post_init__(self):
        if self.eye not in (0, 1):
            raise ValueError('Invalid measurement eye')
        self.lens = self.observation['rig']['eyes'][self.eye]
        self.optical = matrix(self.lens['head_from_optical'])
        self.intrinsics = np.asarray(self.lens['intrinsics'], dtype=np.float64)
        self.coefficients = np.asarray(self.lens['k'], dtype=np.float64)
        self.timing = self.observation['timing'][self.eye]
        self.sample_offset = sampling_offset(self.observation)
        self.image = np.frombuffer(base64.b64decode(
            self.observation['images'][self.eye], validate=True), np.uint8).reshape(616, 616)

    def row_transforms(self, pixels):
        pixels = np.asarray(pixels, dtype=np.float64).reshape(-1, 2)
        if not np.isfinite(pixels).all() or np.any(pixels < 0) or np.any(pixels > 615):
            raise ValueError('Measurement pixels lie outside their source image')
        rows = pixels[:, 1]*4+self.sample_offset
        times = self.timing['first_row_ns']+np.rint(rows*4360).astype(np.int64)
        rotation, translation = row_poses(self.observation['poses'], times)
        optical_rotation = np.einsum('nij,jk->nik', rotation, self.optical[:, :3])
        origin = np.einsum('nij,j->ni', rotation, self.optical[:, 3])+translation
        return optical_rotation, origin

    def rays(self, pixels):
        pixels = np.asarray(pixels, dtype=np.float64).reshape(-1, 2)
        local, valid = unproject_kb(pixels*4+self.sample_offset, self.intrinsics, self.coefficients)
        rotation, origin = self.row_transforms(pixels)
        return origin, np.einsum('nij,nj->ni', rotation, local), valid

    def project_at_observed_rows(self, points, pixels):
        """Reproject using the actual measured row, not a current/centre pose."""
        pixels = np.asarray(pixels, dtype=np.float64).reshape(-1, 2)
        points = np.broadcast_to(np.asarray(points, dtype=np.float64), (len(pixels), 3))
        rotation, origin = self.row_transforms(pixels)
        local = np.einsum('nji,nj->ni', rotation, points-origin)
        return (project_kb(local, self.intrinsics, self.coefficients)-self.sample_offset)/4


def observation_cameras(observation, frame):
    # Reuse every existing optical, exposure-uncertainty and historical-pose
    # admission check. The small audit view grants no reconstruction authority.
    rectify(observation, 32, 32)
    rig = observation['rig']
    centres = [matrix(eye['head_from_optical'])[:, 3] for eye in rig['eyes']]
    if abs(np.linalg.norm(centres[1]-centres[0])-rig['baseline_m']) > 1e-5:
        raise ValueError('Claimed metric baseline disagrees with optical centres')
    return [ObservationCamera(observation, eye, frame) for eye in (0, 1)]


def intersect_rays(origins, directions, minimum_angle_degrees=.3):
    """Metric least-squares intersection; refuse an unobservable depth."""
    origins = np.asarray(origins, dtype=np.float64)
    directions = np.asarray(directions, dtype=np.float64)
    if (origins.shape != directions.shape or origins.ndim != 2 or origins.shape[1] != 3
            or len(origins) < 2 or not np.isfinite(origins).all() or not np.isfinite(directions).all()):
        raise ValueError('Invalid multi-view measurement rays')
    length = np.linalg.norm(directions, axis=1)
    if np.any(length < 1e-12):
        raise ValueError('Empty measurement ray')
    directions = directions/length[:, None]
    angle = np.arccos(np.clip(directions @ directions.T, -1, 1)).max()
    if angle < np.deg2rad(minimum_angle_degrees):
        return None
    projection = np.eye(3)[None]-directions[:, :, None]*directions[:, None, :]
    system = projection.sum(axis=0)
    eigenvalues = np.linalg.eigvalsh(system)
    if eigenvalues[0] < eigenvalues[-1]*1e-6:
        return None
    point = np.linalg.solve(system, np.einsum('nij,nj->i', projection, origins))
    if np.any(np.sum((point-origins)*directions, axis=1) <= 0):
        return None
    return point


def epipolar_error_degrees(a, b, pixels_a, pixels_b):
    oa, da, va = a.rays(pixels_a)
    ob, db, vb = b.rays(pixels_b)
    plane = np.cross(ob-oa, da)
    norm = np.linalg.norm(plane, axis=1)
    sine = abs(np.sum(plane*db, axis=1))/np.maximum(norm, 1e-15)
    error = np.rad2deg(np.arcsin(np.clip(sine, 0, 1)))
    error[~(va & vb & (norm > 1e-9))] = np.nan
    return error


def fit_track(cameras, measurements):
    """Joint point fit with fixed metric cameras and row-time reprojection.

    No similarity alignment to a reference room and no calibration adjustment
    are allowed here. The known room height is reserved for later evaluation.
    """
    origins, rays, factors = [], [], []
    for camera_id, pixel in measurements:
        origin, ray, valid = cameras[camera_id].rays([pixel])
        if not valid[0]:
            return None
        origins.append(origin[0]); rays.append(ray[0])
        rotation, _ = cameras[camera_id].row_transforms([pixel])
        camera = cameras[camera_id]
        factors.append((rotation[0], origin[0], camera.intrinsics, camera.coefficients,
                        np.asarray(pixel), camera.sample_offset))
    initial = intersect_rays(origins, rays)
    if initial is None:
        return None

    def residual(point):
        return np.concatenate([(project_kb(rotation.T @ (point-origin), intrinsics, k)-offset)/4-pixel
                               for rotation, origin, intrinsics, k, pixel, offset in factors])

    result = least_squares(residual, initial, loss='huber', f_scale=1., max_nfev=30)
    if not result.success or not np.isfinite(result.x).all():
        return None
    if np.any(np.sum((result.x-np.asarray(origins))*np.asarray(rays), axis=1) <= 0):
        return None
    return result.x, np.linalg.norm(residual(result.x).reshape(-1, 2), axis=1)


def measured_room_height(points, tolerance_m=.025, minimum_support=20):
    """Measure two supported horizontal surfaces without a reference-height prior.

    A row of features on a picture frame is insufficient support for a room
    plane. Both horizontal spans must be observed, and one plane alone cannot
    establish the floor-to-ceiling distance.
    """
    points = np.asarray(points, dtype=np.float64).reshape(-1, 3)
    points = points[np.isfinite(points).all(axis=1)]
    planes, candidates = [], []
    if len(points) < minimum_support*2:
        return {'height_m': None, 'reason': 'insufficient_floor_ceiling_support', 'planes': planes}
    # A horizontal slice through vertical walls also has horizontal extent.
    # Require local measured surface orientation before classifying a point
    # as floor/ceiling support. Neither fitting nor filling a room plane occurs.
    distance, neighbours = cKDTree(points).query(points, k=12, distance_upper_bound=.5)
    present = np.isfinite(distance)
    nearby = points[np.minimum(neighbours, len(points)-1)]
    count = present.sum(axis=1)
    centre = np.sum(nearby*present[..., None], axis=1)/np.maximum(count, 1)[:, None]
    delta = np.where(present[..., None], nearby-centre[:, None], 0)
    covariance = np.einsum('nki,nkj->nij', delta, delta)/np.maximum(count, 1)[:, None, None]
    values, vectors = np.linalg.eigh(covariance)
    horizontal = (count>=6)&(values[:, 1]>.0001)&(values[:, 0]<values[:, 1]*.1)&(abs(vectors[:, 1, 0])>.95)
    points = points[horizontal]
    if len(points) < minimum_support*2:
        return {'height_m': None, 'reason': 'insufficient_floor_ceiling_support', 'planes': planes}
    for height in np.unique(np.round(points[:, 1]/tolerance_m)*tolerance_m):
        support = points[abs(points[:, 1]-height) <= tolerance_m]
        if len(support) < minimum_support:
            continue
        span = np.quantile(support[:, (0, 2)], .9, axis=0)-np.quantile(support[:, (0, 2)], .1, axis=0)
        if np.min(span) < .4:
            continue
        if np.linalg.eigvalsh(np.cov(support[:, (0, 2)].T)).min() < .01:
            continue
        candidate = {'height_m': float(np.median(support[:, 1])), 'support': len(support),
                     'residual_p95_m': float(np.quantile(abs(support[:, 1]-np.median(support[:, 1])), .95)),
                     'horizontal_span_m': span.tolist()}
        candidates.append(candidate)
    for candidate in sorted(candidates, key=lambda p: p['support'], reverse=True):
        if any(abs(candidate['height_m']-p['height_m']) < tolerance_m*2 for p in planes):
            continue
        planes.append(candidate)
    pairs = [(a, b) for a in planes for b in planes if 1.8 <= b['height_m']-a['height_m'] <= 5]
    if not pairs:
        return {'height_m': None, 'reason': 'insufficient_floor_ceiling_support', 'planes': planes}
    if len(pairs) != 1:
        return {'height_m': None, 'reason': 'ambiguous_horizontal_surfaces', 'planes': planes}
    floor, ceiling = pairs[0]
    return {'height_m': ceiling['height_m']-floor['height_m'], 'reason': 'measured',
            'planes': [floor, ceiling]}
