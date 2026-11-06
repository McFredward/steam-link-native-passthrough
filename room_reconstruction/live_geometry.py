"""Rolling-exposure rectification and metric rays; no current-pose substitution."""
from __future__ import annotations

import base64
import numpy as np
from scipy.ndimage import minimum_filter, gaussian_filter
from scipy.spatial.transform import Rotation
from functools import lru_cache


def sampling_profile(observation):
    encoding = observation.get('encoding')
    if encoding == 'gray8-point-step4':
        offset, area, step, extent = 0., 1, 4, 616
        if 'sampling' not in observation:
            if (observation.get('width'), observation.get('height')) != (extent, extent):
                raise ValueError('Stereo sampling extent changed')
            return step, offset, extent
    elif encoding == 'gray8-area4-step4':
        offset, area, step, extent = 1.5, 4, 4, 616
    elif encoding == 'gray8-area2-step2':
        offset, area, step, extent = .5, 2, 2, 1232
    else:
        raise ValueError('Unsupported stereo pixel sampling')
    sampling = observation.get('sampling')
    expected = {'step': step, 'area': [area, area], 'center_offset': [offset, offset]}
    if (sampling != expected or type(sampling['step']) is not int
            or any(type(v) is not int for v in sampling['area'])
            or any(type(v) not in (int, float) for v in sampling['center_offset'])):
        raise ValueError('Stereo pixel centers are not verified')
    if (observation.get('width'), observation.get('height')) != (extent, extent):
        raise ValueError('Stereo sampling extent changed')
    return step, offset, extent


def sampling_offset(observation):
    return sampling_profile(observation)[1]


@lru_cache(maxsize=3)
def image_grid(width,height,focal_ratio=220/320):
    y,x=np.mgrid[:height,:width]
    focal=width*focal_ratio
    rays=np.stack(((x-(width-1)/2)/focal,-(y-(height-1)/2)/focal,-np.ones_like(x)),axis=-1)
    for value in (x,y,rays):value.flags.writeable=False
    return y,x,rays


def matrix(value):
    result = np.asarray(value, dtype=np.float64).reshape(3, 4)
    rotation = result[:, :3]
    if (not np.isfinite(result).all() or np.max(np.abs(result)) > 1000
            or not np.allclose(rotation.T @ rotation, np.eye(3), atol=2e-4)
            or abs(np.linalg.det(rotation)-1) > 2e-4):
        raise ValueError("Invalid metric capture transform")
    return result


def row_poses(samples, times):
    ticks = np.asarray([p["raw_ns"] for p in samples], dtype=np.int64)
    if (len(samples) != 9 or np.any(np.diff(ticks) <= 0) or ticks[-1]-ticks[0] > 20000000
            or times.min() < ticks[0] or times.max() > ticks[-1]
            or any(not 0 <= p["query_ns"] <= 750000 for p in samples)):
        raise ValueError("Unqualified exposure pose history")
    transforms = np.stack([matrix(p["head"]) for p in samples])
    quaternions = Rotation.from_matrix(transforms[:, :, :3]).as_quat()
    index = np.clip(np.searchsorted(ticks, times, side="right")-1, 0, 7)
    t = (times-ticks[index]) / (ticks[index+1]-ticks[index])
    a, b = quaternions[index], quaternions[index+1]
    b = b*np.where(np.sum(a*b, axis=1) < 0, -1, 1)[:, None]
    q = a*(1-t[:, None])+b*t[:, None]
    q /= np.linalg.norm(q, axis=1)[:, None]
    return Rotation.from_quat(q).as_matrix(), transforms[index, :, 3]*(1-t[:, None])+transforms[index+1, :, 3]*t[:, None]


def project_kb(ray, intrinsics, k):
    r = np.linalg.norm(ray[..., :2], axis=-1)
    theta = np.arctan2(r, ray[..., 2])
    t2 = theta*theta
    scale = theta*(1+t2*(k[0]+t2*(k[1]+t2*(k[2]+t2*k[3]))))/np.maximum(r, 1e-12)
    uv = ray[..., :2]*scale[..., None]*intrinsics[:2]+intrinsics[2:]
    return np.where((ray[..., 2] > 0)[..., None], uv, -100)


def rectify(observation, width, height, *, view_pitch_radians=0., focal_ratio=220/320):
    # Rotate a virtual rectified view about the physical stereo baseline.
    # This preserves horizontal epipolar geometry while reaching peripheral
    # ceiling/floor content. Physical lenses and exposure poses stay unchanged.
    if (not np.isfinite(view_pitch_radians) or abs(view_pitch_radians) > .65
            or not np.isfinite(focal_ratio) or not .5 <= focal_ratio <= 1.):
        raise ValueError('Unsupported virtual stereo view')
    sample_step, sample_offset, extent = sampling_profile(observation)
    if (observation["schema"] != "sfnp-room-stereo-v1"
            or len(observation["images"]) != 2 or len(observation["timing"]) != 2):
        raise ValueError("Unsupported stereo observation")
    reference = matrix(observation["reference_head"])
    rig = observation["rig"]
    rectified = matrix(rig["head_from_rectified"])
    if view_pitch_radians:
        cosine, sine = np.cos(view_pitch_radians), np.sin(view_pitch_radians)
        pitch = np.array(((1., 0., 0.), (0., cosine, -sine), (0., sine, cosine)))
        rectified = np.column_stack((rectified[:, :3] @ pitch, rectified[:, 3]))
    if not .03 <= rig["baseline_m"] <= .10 or len(rig["eyes"]) != 2:
        raise ValueError("Unverified stereo rig")
    room = np.column_stack((reference[:, :3] @ rectified[:, :3], reference[:, :3] @ rectified[:, 3]+reference[:, 3]))
    focal = width*focal_ratio
    cx, cy = (width-1)/2, (height-1)/2
    y,x,rays=image_grid(width,height,focal_ratio)
    world_rays = np.einsum('...j,ij->...i',rays,room[:,:3])
    head_rays = np.einsum('...i,ij->...j',world_rays,reference[:,:3])
    images, masks, origins = [], [], []
    for index, eye in enumerate(rig["eyes"]):
        raw = base64.b64decode(observation["images"][index], validate=True)
        if len(raw) != extent*extent:
            raise ValueError("Stereo image extent changed")
        image = np.frombuffer(raw, dtype=np.uint8).reshape(extent, extent)
        lens = matrix(eye["head_from_optical"])
        intrinsics, k = np.asarray(eye["intrinsics"]), np.asarray(eye["k"])
        if (intrinsics.shape != (4,) or k.shape != (4,) or not np.isfinite(k).all()
                or not np.isfinite(intrinsics).all() or np.any(intrinsics[:2] < 500)):
            raise ValueError("Unverified optical calibration")
        uv = (project_kb(np.einsum('...i,ij->...j',head_rays,lens[:,:3]), intrinsics, k)-sample_offset)/sample_step
        timing = observation["timing"][index]
        if timing["line_ns"] != 4360 or not 0 < timing["integration_ns"] < 40000000:
            raise ValueError("Unsupported row timing")
        uncertainty=timing.get('uncertainty_ns',0)
        center_bound=timing.get('uncertainty_kind')=='exposure-center-v1'
        query_budget=max(p['query_ns'] for p in observation['poses'])
        if (type(uncertainty)is not int or not 0<=uncertainty<=(1500000 if center_bound else 250000)
                or query_budget>750000 or not center_bound and query_budget+uncertainty>750000):
            raise ValueError('Exposure timing uncertainty exceeds budget')
        if uncertainty or center_bound:
            minimum,maximum=timing['integration_min_ns'],timing['integration_max_ns']
            expected=(0 if minimum==maximum else (maximum-minimum+3)//4+1) if center_bound else (maximum-minimum+1)//2
            if not 0<minimum<=timing['integration_ns']<=maximum<40000000 or expected!=uncertainty:
                raise ValueError('Unverified exposure interval')
            history=np.stack([matrix(p['head']) for p in observation['poses']])
            if center_bound:
                first=timing['first_row_ns'];last=first+2463*4360
                if observation['poses'][0]['raw_ns']>first-uncertainty or observation['poses'][-1]['raw_ns']<last+uncertainty:
                    raise ValueError('Exposure uncertainty is outside pose history')
            dt=np.diff(np.asarray([p['raw_ns'] for p in observation['poses']],dtype=np.int64))*1e-9
            if len(dt)!=8 or np.any(dt<=0):raise ValueError('Unqualified exposure pose history')
            rotation=Rotation.from_matrix(history[:,:,:3])
            spatial_time=(uncertainty+query_budget if center_bound else uncertainty)*1e-9
            if (np.max(np.linalg.norm(np.diff(history[:,:,3],axis=0),axis=1)/dt)*spatial_time>.0005
                    or np.max((rotation[:-1].inv()*rotation[1:]).magnitude()/dt)*spatial_time>.0005):
                raise ValueError('Exposure interval has too much spatial uncertainty')
        rotations, translations = row_poses(observation["poses"], timing["first_row_ns"]+np.arange(2464, dtype=np.int64)*4360)
        for _ in range(2):
            rows = np.clip((uv[..., 1]*sample_step+sample_offset+.5).astype(np.int32), 0, 2463)
            at_head = np.einsum("...ji,...j->...i", rotations[rows], world_rays)
            uv = (project_kb(np.einsum('...i,ij->...j',at_head,lens[:,:3]), intrinsics, k)-sample_offset)/sample_step
        valid = np.isfinite(uv).all(axis=-1) & (uv[..., 0] >= 0) & (uv[..., 1] >= 0) & (uv[..., 0] < extent-1) & (uv[..., 1] < extent-1)
        safe = np.clip(uv, 0, extent-1.001)
        ix, iy = safe[..., 0].astype(int), safe[..., 1].astype(int)
        a, b = safe[..., 0]-ix, safe[..., 1]-iy
        pixels = (1-b)*((1-a)*image[iy, ix]+a*image[iy, ix+1])+b*((1-a)*image[iy+1, ix]+a*image[iy+1, ix+1])
        images.append(np.where(valid, pixels, 0).astype(np.uint8))
        masks.append(minimum_filter(valid.astype(np.uint8), size=7, mode="constant", cval=0).astype(bool))
        rows = np.clip((safe[..., 1]*sample_step+sample_offset+.5).astype(np.int32), 0, 2463)
        origins.append(np.einsum("...ij,j->...i", rotations[rows], lens[:, 3])+translations[rows])
    return images, masks, origins, world_rays, room, (focal, focal, cx, cy)


def surface_normals(points, depth, valid, rays):
    """Estimate local orientation without amplifying individual disparity pixels.

    Inverse depth is affine on a plane, including a sloping floor or ceiling.
    Reject depth edges by its local prediction error, rather than rejecting
    every large first derivative at a grazing angle. Positions stay measured.
    """
    inverse = 1/np.maximum(depth, .01)
    measured=valid.copy()
    for axis in (0, 1):
        predicted = 2/np.maximum(np.roll(inverse, 1, axis)+np.roll(inverse, -1, axis), 1e-12)
        neighbours=np.roll(measured,1,axis)&np.roll(measured,-1,axis)
        valid &= ~neighbours|(np.abs(depth-predicted) < .025+.015*depth)
    weight = gaussian_filter(valid.astype(np.float32), 1.5, truncate=2, mode='constant')
    local = np.stack([gaussian_filter(np.where(valid, points[..., i], 0).astype(np.float32), 1.5,
                                     truncate=2, mode='constant') for i in range(3)], -1)
    local /= np.maximum(weight, 1e-12)[..., None]
    normal = np.cross(np.roll(local, -2, 1)-np.roll(local, 2, 1),
                      np.roll(local, -2, 0)-np.roll(local, 2, 0))
    norm = np.linalg.norm(normal, axis=-1)
    normal /= np.maximum(norm, 1e-12)[..., None]
    normal *= np.where(np.sum(normal*rays, axis=-1) > 0, -1, 1)[..., None]
    # Estimate orientation from supported neighbours without requiring every
    # pixel of a 7x7 patch to pass independent confidence checks. Never turn
    # an unmeasured centre into measured geometry or move its measured point.
    valid &= (norm > 1e-7) & (weight > .75)
    valid &= np.abs(np.sum((points-local)*normal, axis=-1)) < .02+.003*depth
    return normal, valid


def triangulate(disparities, masks, origins, rays, room, intrinsics, *, lr_tolerance=.85, diagnostics=None):
    left, right = disparities
    height, width = left.shape
    if right.shape != left.shape or not np.isfinite(left).all() or not np.isfinite(right).all():
        raise ValueError("Invalid neural disparity output")
    y, x = np.mgrid[:height, :width]
    ru = x-left
    lo = np.clip(np.floor(ru).astype(int), 0, width-2)
    t = np.clip(ru-lo, 0, 1)
    sample = lambda a: a[y, lo]*(1-t)[..., None]+a[y, lo+1]*t[..., None]
    rd = right[y, lo]*(1-t)+right[y, lo+1]*t
    valid = masks[0] & masks[1][y, lo] & masks[1][y, lo+1] & (ru >= 0) & (ru < width-1) & (left > .1)
    valid &= np.abs(left-rd) < lr_tolerance
    if diagnostics is not None:diagnostics['bilateral']=int(valid.sum())
    a = rays / np.linalg.norm(rays, axis=-1)[..., None]
    b = sample(rays)
    b /= np.linalg.norm(b, axis=-1)[..., None]
    delta = sample(origins[1])-origins[0]
    ab = np.sum(a*b, axis=-1)
    da, db = np.sum(delta*a, axis=-1), np.sum(delta*b, axis=-1)
    denominator = np.maximum(1-ab*ab, 1e-12)
    ta, tb = (da-ab*db)/denominator, (ab*da-db)/denominator
    pa, pb = origins[0]+ta[..., None]*a, sample(origins[1])+tb[..., None]*b
    points = (pa+pb)/2
    local = np.einsum('...i,ij->...j',points-room[:,3],room[:,:3])
    depth = -local[..., 2]
    valid &= (ta > 0) & (tb > 0) & (depth >= .25) & (depth <= 6) & (np.linalg.norm(pa-pb, axis=-1) < .006)
    if diagnostics is not None:diagnostics['metric']=int(valid.sum())
    normal, valid = surface_normals(points, depth, valid, a)
    if diagnostics is not None:diagnostics['surface']=int(valid.sum())
    return points, normal, valid, depth
