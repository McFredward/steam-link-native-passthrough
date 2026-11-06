"""Metric geometry checks; appearance scores never substitute for surfaces."""
import numpy as np
from scipy.spatial import cKDTree


def read_native_mesh(path):
    """Read the bounded binary XYZ/normal triangle output of the pinned trainer."""
    with open(path, 'rb') as source:
        lines = []
        for _ in range(64):
            line = source.readline(1025)
            if not line or len(line)>1024:
                raise ValueError('Invalid metric mesh header')
            lines.append(line.decode('ascii').strip())
            if line == b'end_header\n':
                break
        if lines[:2] != ['ply', 'format binary_little_endian 1.0'] or lines[-1] != 'end_header':
            raise ValueError('Unsupported metric mesh format')
        count = next(int(line.split()[2]) for line in lines if line.startswith('element vertex '))
        faces = next(int(line.split()[2]) for line in lines if line.startswith('element face '))
        properties = [line for line in lines if line.startswith('property ')]
        if (properties != ['property float '+v for v in ('x','y','z','nx','ny','nz')]
                +['property list uchar int vertex_indices'] or not 0<count<=1000000 or not 0<faces<=2000000):
            raise ValueError('Unexpected metric mesh layout or extent')
        vertices = np.frombuffer(source.read(count*24), '<f4').reshape(count, 6)[:, :3].copy()
        data = np.frombuffer(source.read(faces*13), dtype=np.dtype([('n','u1'),('indices','<i4',(3,))]))
        if len(data)!=faces or (data['n']!=3).any() or source.read(1):
            raise ValueError('Incomplete metric mesh faces')
        triangles = data['indices'].copy()
    if not np.isfinite(vertices).all() or triangles.min()<0 or triangles.max()>=count:
        raise ValueError('Invalid metric mesh geometry')
    return vertices, triangles


def surface_samples(vertices, triangles, spacing_m=.05, maximum=1000000):
    p = np.asarray(vertices)[triangles]
    edges = np.linalg.norm(p-np.roll(p, 1, axis=1), axis=2).max(axis=1)
    samples = []
    count = 0
    for triangle, length in zip(p, edges):
        steps = max(1, int(np.ceil(length/spacing_m)))
        if steps>128:
            raise ValueError('Metric mesh contains an unexpectedly large face')
        i, j = np.mgrid[:steps+1, :steps+1]; valid = i+j<=steps
        a, b = i[valid]/steps, j[valid]/steps
        sample = triangle[0]+a[:, None]*(triangle[1]-triangle[0])+b[:, None]*(triangle[2]-triangle[0])
        count += len(sample)
        if count>maximum:
            raise ValueError('Metric surface sample budget reached')
        samples.append(sample)
    return np.concatenate(samples) if samples else np.empty((0, 3))


def box_metrics(vertices, triangles, witnesses, height_m):
    points = surface_samples(vertices, triangles)
    distances = np.min(np.stack((abs(points[:,0]+2), abs(points[:,0]-2), abs(points[:,2]+2),
                                 abs(points[:,2]-2), abs(points[:,1]), abs(points[:,1]-height_m))), axis=0)
    outside = np.linalg.norm(points-np.clip(points, (-2,0,-2), (2,height_m,2)), axis=1)
    distances = np.where(outside>0, outside, distances)
    nearest = cKDTree(points).query(witnesses, k=1)[0]
    return {'surface_samples':len(points), 'known_surface_distance_median_m':float(np.median(distances)),
            'known_surface_distance_p95_m':float(np.quantile(distances,.95)),
            'observed_surface_completeness_within_8cm':float(np.mean(nearest<.08)),
            'observed_surface_distance_p95_m':float(np.quantile(nearest,.95)),
            'ground_truth_alignment_fitted':False, 'scope':'synthetic known room, not real Frame accuracy'}
