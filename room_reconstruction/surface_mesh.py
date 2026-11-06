"""Connect qualified surface-ray measurements without a density isosurface.

Only neighbouring measured samples may form a triangle. Unknown samples,
occlusion jumps and inconsistent normals remain explicit boundaries. The caller
must supply its independently qualified evidence mask; rendering alpha alone
does not qualify physical geometry.
"""
from __future__ import annotations

import numpy as np


def measured_ray_mesh(origins, directions, distances, normals, evidence, *,
                      width, height, max_edge_m=.3, max_surface_residual_m=.02):
    count = width*height
    if (type(width) is not int or type(height) is not int or not 2 <= width <= 4096
            or not 2 <= height <= 4096 or count > 1000000
            or not .005 <= max_edge_m <= .5 or not .001 <= max_surface_residual_m <= .05):
        raise ValueError('Invalid measured surface grid budget')
    origins = np.asarray(origins, dtype=np.float64)
    directions = np.asarray(directions, dtype=np.float64)
    distances = np.asarray(distances, dtype=np.float64)
    normals = np.asarray(normals, dtype=np.float64)
    evidence = np.asarray(evidence)
    if (origins.shape != (count, 3) or directions.shape != (count, 3)
            or distances.shape != (count,) or normals.shape != (count, 3)
            or evidence.shape != (count,) or evidence.dtype != bool):
        raise ValueError('Measured surface arrays disagree with their grid')
    length = np.linalg.norm(directions, axis=-1)
    normal_length = np.linalg.norm(normals, axis=-1)
    valid = (evidence & np.isfinite(origins).all(-1) & np.isfinite(directions).all(-1)
             & np.isfinite(normals).all(-1) & np.isfinite(distances)
             & (length > .9) & (normal_length > .5) & (distances >= .2) & (distances <= 6.))
    direction = directions/np.maximum(length, 1e-12)[:, None]
    normal = normals/np.maximum(normal_length, 1e-12)[:, None]
    points = origins+direction*distances[:, None]
    indices = np.arange(count).reshape(height, width)
    a = indices[:-1, :-1].ravel(); b = a+1; c = a+width; d = c+1
    faces = np.concatenate((np.stack((a, c, b), -1), np.stack((b, c, d), -1)))
    accepted = valid[faces].all(-1)
    # Both measured endpoint normals must support each edge. This rejects a
    # short bridge between two parallel surfaces at different physical depths.
    for i, j in ((0, 1), (1, 2), (2, 0)):
        left, right = faces[:, i], faces[:, j]
        delta = points[right]-points[left]
        accepted &= np.linalg.norm(delta, axis=-1) <= max_edge_m
        accepted &= np.sum(normal[left]*normal[right], axis=-1) >= .85
        accepted &= abs(np.sum(delta*normal[left], axis=-1)) <= max_surface_residual_m
        accepted &= abs(np.sum(delta*normal[right], axis=-1)) <= max_surface_residual_m
    faces = faces[accepted]
    if not len(faces):
        return np.empty((0, 3)), np.empty((0, 3), np.int32)
    cross = np.cross(points[faces[:, 1]]-points[faces[:, 0]], points[faces[:, 2]]-points[faces[:, 0]])
    faces = faces[np.linalg.norm(cross, axis=-1) > 1e-10]
    if not len(faces):
        return np.empty((0, 3)), np.empty((0, 3), np.int32)
    cross = np.cross(points[faces[:, 1]]-points[faces[:, 0]], points[faces[:, 2]]-points[faces[:, 0]])
    reverse = np.sum(cross*normal[faces].sum(1), axis=-1) < 0
    faces[reverse] = faces[reverse, ::-1]
    used, remap = np.unique(faces, return_inverse=True)
    return points[used], remap.reshape(-1, 3).astype(np.int32)
