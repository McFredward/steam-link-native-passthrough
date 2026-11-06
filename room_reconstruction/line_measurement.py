"""Metric room-edge measurements with actual rolling-row cameras.

A line has no assumed endpoint correspondence between views. Each measured
image sample constrains its perpendicular projection only. Returned intervals
retain individual observations; this module never fills an unseen plane or
uses a room dimension. Transverse uncertainty is conditional on the supplied
cameras and half-pixel line noise, not independent proof of camera calibration.
"""
from __future__ import annotations

from dataclasses import dataclass
import numpy as np
from scipy.optimize import least_squares

from .live_geometry import project_kb


@dataclass
class MeasuredLine:
    point: np.ndarray
    direction: np.ndarray
    residual_pixels: np.ndarray
    observation_intervals: list
    transverse_sigma_m: float
    jacobian_condition: float


def _basis(direction):
    direction = np.asarray(direction, dtype=np.float64).copy()
    direction /= np.linalg.norm(direction)
    seed = np.eye(3)[np.argmin(abs(direction))]
    u = np.cross(direction, seed); u /= np.linalg.norm(u)
    return direction, u, np.cross(direction, u)


def _factors(cameras, measurements):
    if not 1 <= len(cameras) <= 512 or not 1 <= len(measurements) <= 128:
        raise ValueError('Line measurement dataset exceeds its explicit budget')
    result, seen = [], set()
    for index, pixels in measurements:
        if (not isinstance(index, (int, np.integer)) or isinstance(index, bool)
                or not 0 <= index < len(cameras) or index in seen):
            raise ValueError('Invalid or repeated line camera')
        seen.add(index)
        pixels = np.asarray(pixels, dtype=np.float64)
        if (pixels.ndim != 2 or pixels.shape[1] != 2 or not 3 <= len(pixels) <= 32
                or not np.isfinite(pixels).all() or np.linalg.norm(np.ptp(pixels, axis=0)) < 2):
            raise ValueError('Invalid or unbounded measured line samples')
        camera = cameras[index]
        origins, rays, valid = camera.rays(pixels)
        if not valid.all():
            raise ValueError('Measured line lies outside the calibrated lens branch')
        rotations, _ = camera.row_transforms(pixels)
        result.append((int(index), camera, pixels, origins, rays, rotations))
    return result


def _closest(point, direction, origins, rays):
    w = point-origins; c = rays @ direction; denominator = 1-c*c
    distance = (np.sum(rays*w, axis=1)-c*(w @ direction))/np.maximum(denominator, 1e-12)
    parameter = c*distance-w @ direction
    return point+parameter[:, None]*direction, distance, parameter, denominator


def _prediction(point, direction, factors):
    values, intervals = [], []
    for index, camera, pixels, origins, rays, rotations in factors:
        _, _, parameter, denominator = _closest(point, direction, origins, rays)
        local_origin = np.einsum('nji,nj->ni', rotations, point-origins)
        local_direction = np.einsum('nji,j->ni', rotations, direction)
        # A distorted lens projects a world line to a curve. Refine each
        # sample's nearest point on that curve, at its measured exposure row.
        for iteration in range(3):
            local = local_origin+parameter[:, None]*local_direction
            image = (project_kb(local, camera.intrinsics, camera.coefficients)-camera.sample_offset)/camera.sample_step
            vector = local_direction*.001
            tangent = (project_kb(local+vector, camera.intrinsics, camera.coefficients)
                       -project_kb(local-vector, camera.intrinsics, camera.coefficients))/(.002*camera.sample_step)
            delta = image-pixels
            if iteration < 2:
                step = np.sum(delta*tangent, axis=1)/np.maximum(np.sum(tangent*tangent, axis=1), 1e-12)
                parameter -= np.clip(step, -1., 1.)
        distance = np.sum((point+parameter[:, None]*direction-origins)*rays, axis=1)
        length = np.linalg.norm(tangent, axis=1)
        side = delta[:, 0]*tangent[:, 1]-delta[:, 1]*tangent[:, 0]
        # At convergence this is the perpendicular distance. Retaining the
        # complete remaining distance also scores an unconverged bad target.
        residual = np.copysign(np.linalg.norm(delta, axis=1), side)
        # Impossible predictions stay explicit; callers must not silently drop
        # them from a withheld score as if no measurement had been made.
        invalid = ((distance <= 0) | (denominator < 1e-8) | (length < 1e-8)
                   | ~np.isfinite(residual))
        residual[invalid] = np.inf
        values.extend(residual)
        intervals.append({'camera': index, 'parameter_min_m': float(parameter.min()),
                          'parameter_max_m': float(parameter.max()),
                          'range_min_m': float(distance.min()), 'range_max_m': float(distance.max()),
                          'minimum_ray_line_sine_squared': float(denominator.min())})
    return np.asarray(values), intervals


def line_prediction_errors(cameras, line, measurements):
    """Score supplied exposures without fitting or rejecting bad predictions."""
    point, direction = np.asarray(line.point), np.asarray(line.direction)
    if (point.shape != (3,) or direction.shape != (3,) or not np.isfinite(point).all()
            or not np.isfinite(direction).all() or abs(np.linalg.norm(direction)-1) > 1e-8):
        raise ValueError('Invalid metric line')
    return _prediction(point, direction, _factors(cameras, measurements))[0]


def fit_measured_line(cameras, measurements):
    """Fit only the supplied training observations; refuse unobservable depth."""
    factors = _factors(cameras, measurements)
    if len({camera.frame for _, camera, *_ in factors}) < 2:
        return None
    planes = []
    for _, _, _, origins, rays, _ in factors:
        # Approximate backprojection planes initialize the fit only. The
        # objective below retains every sample's real row origin and rotation.
        _, _, vh = np.linalg.svd(rays, full_matrices=False)
        normal = vh[-1]
        planes.append((normal, float(np.median(origins @ normal))))
    best = None
    for a, (na, ca) in enumerate(planes):
        for nb, cb in planes[a+1:]:
            direction = np.cross(na, nb); sine = np.linalg.norm(direction)
            if best is None or sine > best[0]:
                best = sine, na, nb, ca, cb, direction
    if best is None or best[0] < np.sin(np.deg2rad(.3)):
        return None
    _, na, nb, ca, cb, initial_direction = best
    initial_direction, u, v = _basis(initial_direction)
    initial_point = np.linalg.solve(np.asarray((na, nb, initial_direction)), (ca, cb, 0.))
    # Samples from one detected segment have correlated offset/angle errors.
    # Densely sampling it must not manufacture independent camera evidence.
    weights = np.concatenate([np.full(len(pixels), np.sqrt(2/len(pixels)))
                              for _, _, pixels, *_ in factors])

    def state(parameters):
        direction = initial_direction+parameters[2]*u+parameters[3]*v
        direction /= np.linalg.norm(direction)
        point = initial_point+parameters[0]*u+parameters[1]*v
        point -= direction*(point @ direction)
        return point, direction

    def residual(parameters):
        value = _prediction(*state(parameters), factors)[0]
        # The solver needs finite trial costs. Invalid final geometry is still
        # refused below; this is not an admission of a behind-camera line.
        return np.nan_to_num(value, nan=1e6, posinf=1e6, neginf=-1e6)*weights

    fit = least_squares(residual, np.zeros(4), loss='huber', f_scale=.5, max_nfev=40)
    if not fit.success or not np.isfinite(fit.x).all():
        return None
    point, direction = state(fit.x)
    errors, intervals = _prediction(point, direction, factors)
    _, singular, vh = np.linalg.svd(fit.jac, full_matrices=False)
    if (not np.isfinite(errors).all() or singular[-1] < max(singular[0]*1e-6, 1e-9)
            or any(i['range_min_m'] < .15 or i['range_max_m'] > 8
                   or i['minimum_ray_line_sine_squared'] < 1e-4 for i in intervals)):
        return None
    covariance = (vh.T*(.5/singular)**2) @ vh
    middle = np.median([(i['parameter_min_m']+i['parameter_max_m'])/2 for i in intervals])
    base = point+middle*direction
    derivative = []
    for j in range(4):
        parameters = fit.x.copy(); parameters[j] += 1e-5
        location, vector = state(parameters)
        # Compare the nearest position on the perturbed infinite line. A
        # change of parameter origin along that line is not a depth error.
        nearest = location+((base-location) @ vector)*vector
        derivative.append((nearest-base)/1e-5)
    derivative = np.asarray(derivative).T
    sigma = np.sqrt(max(np.linalg.eigvalsh(derivative @ covariance @ derivative.T)[-1], 0))
    return MeasuredLine(point, direction, errors, intervals, float(sigma), float(singular[0]/singular[-1]))
