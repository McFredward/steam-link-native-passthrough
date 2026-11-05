"""Union overlapping measured planar patches without replacing openings by a hull."""
from __future__ import annotations

import numpy as np


def merge_measured(points, normals, quality, faces):
    """Return a mesh of the same observed support, with bounded plane refinement.

    Each local group must fit within 2 cm of its measured vertices and agree
    with their normals. Nonplanar groups retain their original triangles.
    Polygon union removes overlapping internal borders; constrained triangles
    retain every unobserved opening in that union.
    """
    import shapely

    p = points[faces].astype(np.float64)
    center = p.mean(axis=1)
    direction = normals[faces].mean(axis=1).astype(np.float64)
    direction /= np.maximum(np.linalg.norm(direction, axis=1)[:, None], 1e-12)
    axis = np.argmax(abs(direction), axis=1)
    direction *= np.where(direction[np.arange(len(axis)), axis] < 0, -1, 1)[:, None]
    keys = np.column_stack((np.floor(center/.4).astype(np.int32), np.rint(direction*4).astype(np.int32)))
    _, groups = np.unique(keys, axis=0, return_inverse=True)
    order = np.argsort(groups, kind='stable')
    starts = np.r_[0, np.flatnonzero(np.diff(groups[order]))+1, len(order)]
    output_points, output_normals, output_quality, output_faces = [], [], [], []
    offset = 0
    for start, end in zip(starts[:-1], starts[1:]):
        selected = order[start:end]
        original = p[selected]
        reference = direction[selected].mean(axis=0)
        reference /= max(np.linalg.norm(reference), 1e-12)
        origin = original.reshape(-1, 3).mean(axis=0)
        relative = original-origin
        _, basis = np.linalg.eigh(relative.reshape(-1, 3).T @ relative.reshape(-1, 3))
        normal = basis[:, 0]
        if normal @ reference < 0: normal = -normal
        planar = (len(selected) >= 4 and np.max(abs(relative @ normal)) <= .02
                  and np.min(direction[selected] @ normal) >= .96)
        candidate = None
        if planar:
            # Preserve the two actual world coordinates bit for bit. Rotating
            # into a fitted eigenbasis introduces tiny gaps along shared edges
            # and makes repeated union accumulate artificial boundaries.
            drop = int(np.argmax(abs(normal)))
            axes = [i for i in range(3) if i != drop]
            projected = original[..., axes]
            try:
                measured = shapely.union_all(shapely.polygons(projected))
                # Zero tolerance removes only exactly collinear redundant points.
                # A positive simplification radius could shrink a real opening.
                measured = shapely.simplify(measured, 0, preserve_topology=True)
                triangulated = shapely.get_parts(shapely.constrained_delaunay_triangles(measured))
            except shapely.errors.GEOSException:
                # A local numerical failure cannot discard its measured patch.
                triangulated = []
            if len(triangulated):
                xy = shapely.get_coordinates(triangulated).reshape(-1, 4, 2)[:, :3]
                candidate = np.empty((*xy.shape[:2], 3), np.float64)
                candidate[..., axes] = xy
                candidate[..., drop] = (normal @ origin-xy @ normal[axes])/normal[drop]
                if len(candidate) > len(original): candidate = None
        if candidate is None:
            vertices, inverse = np.unique(faces[selected], return_inverse=True)
            local_points, local_faces = points[vertices], inverse.reshape(-1, 3)
            local_normals, local_quality = normals[vertices], quality[vertices]
        else:
            local_points, inverse = np.unique(candidate.reshape(-1, 3).astype(np.float32), axis=0, return_inverse=True)
            local_faces = inverse.reshape(-1, 3)
            # This is interpolated measured support, never extra confidence.
            local_normals = np.broadcast_to(normal, local_points.shape)
            local_quality = np.full(len(local_points), quality[faces[selected]].min(), np.uint8)
        output_points.append(local_points)
        output_normals.append(local_normals)
        output_quality.append(local_quality)
        output_faces.append(local_faces+offset)
        offset += len(local_points)
    if not output_faces:
        return points[:0], normals[:0], quality[:0], faces[:0]
    points = np.concatenate(output_points).astype(np.float32)
    normals = np.concatenate(output_normals).astype(np.float32)
    quality = np.concatenate(output_quality)
    faces = np.concatenate(output_faces).astype(np.int32)
    # Shared planar borders can share vertices; distinct corner orientations
    # remain independent Gaussian supports at the same measured position.
    keys = np.column_stack((points, np.rint(normals*16)))
    _, keep, inverse = np.unique(keys, axis=0, return_index=True, return_inverse=True)
    reduced_quality = np.full(len(keep), 2, np.uint8)
    np.minimum.at(reduced_quality, inverse, quality)
    faces = inverse[faces]
    p = points[keep][faces]
    valid = np.linalg.norm(np.cross(p[:, 1]-p[:, 0], p[:, 2]-p[:, 0]), axis=1) > 1e-7
    faces = faces[valid]
    used, inverse = np.unique(faces, return_inverse=True)
    return points[keep][used], normals[keep][used], reduced_quality[used], inverse.reshape(-1, 3)
