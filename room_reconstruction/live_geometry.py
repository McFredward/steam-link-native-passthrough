"""Rolling-exposure rectification and metric rays; no current-pose substitution."""
from __future__ import annotations

import base64
import numpy as np
from scipy.ndimage import minimum_filter
from scipy.spatial.transform import Rotation


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


def rectify(observation, width, height):
    if (observation["schema"] != "sfnp-room-stereo-v1" or observation["encoding"] != "gray8-point-step4"
            or (observation["width"], observation["height"]) != (616, 616)
            or len(observation["images"]) != 2 or len(observation["timing"]) != 2):
        raise ValueError("Unsupported stereo observation")
    reference = matrix(observation["reference_head"])
    rig = observation["rig"]
    rectified = matrix(rig["head_from_rectified"])
    if not .03 <= rig["baseline_m"] <= .10 or len(rig["eyes"]) != 2:
        raise ValueError("Unverified stereo rig")
    room = np.column_stack((reference[:, :3] @ rectified[:, :3], reference[:, :3] @ rectified[:, 3]+reference[:, 3]))
    focal = width*220/320
    cx, cy = (width-1)/2, (height-1)/2
    y, x = np.mgrid[:height, :width]
    rays = np.stack(((x-cx)/focal, -(y-cy)/focal, -np.ones_like(x)), axis=-1)
    world_rays = rays @ room[:, :3].T
    head_rays = world_rays @ reference[:, :3]
    images, masks, origins = [], [], []
    for index, eye in enumerate(rig["eyes"]):
        raw = base64.b64decode(observation["images"][index], validate=True)
        if len(raw) != 616*616:
            raise ValueError("Stereo image extent changed")
        image = np.frombuffer(raw, dtype=np.uint8).reshape(616, 616)
        lens = matrix(eye["head_from_optical"])
        intrinsics, k = np.asarray(eye["intrinsics"]), np.asarray(eye["k"])
        if (intrinsics.shape != (4,) or k.shape != (4,) or not np.isfinite(k).all()
                or not np.isfinite(intrinsics).all() or np.any(intrinsics[:2] < 500)):
            raise ValueError("Unverified optical calibration")
        uv = project_kb(head_rays @ lens[:, :3], intrinsics, k)/4
        timing = observation["timing"][index]
        if timing["line_ns"] != 4360 or not 0 < timing["integration_ns"] < 40000000:
            raise ValueError("Unsupported row timing")
        rotations, translations = row_poses(observation["poses"], timing["first_row_ns"]+np.arange(2464, dtype=np.int64)*4360)
        for _ in range(2):
            rows = np.clip((uv[..., 1]*4+.5).astype(np.int32), 0, 2463)
            at_head = np.einsum("...ji,...j->...i", rotations[rows], world_rays)
            uv = project_kb(at_head @ lens[:, :3], intrinsics, k)/4
        valid = np.isfinite(uv).all(axis=-1) & (uv[..., 0] >= 0) & (uv[..., 1] >= 0) & (uv[..., 0] < 615) & (uv[..., 1] < 615)
        safe = np.clip(uv, 0, 614.999)
        ix, iy = safe[..., 0].astype(int), safe[..., 1].astype(int)
        a, b = safe[..., 0]-ix, safe[..., 1]-iy
        pixels = (1-b)*((1-a)*image[iy, ix]+a*image[iy, ix+1])+b*((1-a)*image[iy+1, ix]+a*image[iy+1, ix+1])
        images.append(np.where(valid, pixels, 0).astype(np.uint8))
        masks.append(minimum_filter(valid.astype(np.uint8), size=7, mode="constant", cval=0).astype(bool))
        rows = np.clip((safe[..., 1]*4+.5).astype(np.int32), 0, 2463)
        origins.append(np.einsum("...ij,j->...i", rotations[rows], lens[:, 3])+translations[rows])
    return images, masks, origins, world_rays, room, (focal, focal, cx, cy)


def triangulate(disparities, masks, origins, rays, room, intrinsics):
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
    valid &= np.abs(left-rd) < .85
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
    local = (points-room[:, 3]) @ room[:, :3]
    depth = -local[..., 2]
    valid &= (ta > 0) & (tb > 0) & (depth >= .25) & (depth <= 6) & (np.linalg.norm(pa-pb, axis=-1) < .006)
    normal = np.cross(np.roll(points, -1, axis=1)-np.roll(points, 1, axis=1),
                      np.roll(points, -1, axis=0)-np.roll(points, 1, axis=0))
    norm = np.linalg.norm(normal, axis=-1)
    valid &= norm > 1e-7
    normal /= np.maximum(norm, 1e-12)[..., None]
    normal *= np.where(np.sum(normal*a, axis=-1) > 0, -1, 1)[..., None]
    discontinuity = np.maximum(np.abs(depth-np.roll(depth, 1, axis=0)), np.abs(depth-np.roll(depth, 1, axis=1)))
    valid &= discontinuity < .025+.015*depth
    valid = minimum_filter(valid.astype(np.uint8), size=3, mode="constant", cval=0).astype(bool)
    return points, normal, valid, depth
