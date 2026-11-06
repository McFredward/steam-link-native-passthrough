"""Test a bounded stereo-rotation hypothesis on held-out exposure groups.

The result is diagnostic. It never updates the factory calibration, metric
baseline, tracking, captured packets or any live reconstruction settings.
"""
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation

from .measurement import unproject_kb


def rotation_hypothesis(cameras, pairs):
    factors = []
    for pair in pairs:
        a, b = cameras[pair['a']], cameras[pair['b']]
        pa, pb = pair['pixels_a'], pair['pixels_b']
        oa, da, va = a.rays(pa)
        ob, _, vb = b.rays(pb)
        rb, _ = b.row_transforms(pb)
        local, valid = unproject_kb(pb*4, b.intrinsics, b.coefficients)
        normal = np.cross(ob-oa, da)
        norm = np.linalg.norm(normal, axis=1)
        normal /= np.maximum(norm, 1e-15)[:, None]
        current = np.einsum('nij,nj->ni', rb, local)
        error = np.arcsin(np.clip(np.sum(normal*current, axis=1), -1, 1))
        usable = va & vb & valid & (norm > 1e-9) & (abs(error) < np.deg2rad(1.))
        if usable.sum() >= 8:
            factors.append((a.frame, normal[usable], rb[usable], local[usable], float(b.intrinsics[:2].mean()/4)))
    frames = sorted({f[0] for f in factors})
    if len(frames) < 3:
        return {'status': 'insufficient_independent_exposures', 'applied': False}

    def residual(delta, selected):
        correction = Rotation.from_rotvec(delta).as_matrix()
        return np.concatenate([np.arcsin(np.clip(np.sum(n*np.einsum('nij,nj->ni', r, local @ correction.T), axis=1), -1, 1))*focal
                               for _, n, r, local, focal in selected])

    folds = []
    for held_frame in frames:
        train = [f for f in factors if f[0] != held_frame]
        held = [f for f in factors if f[0] == held_frame]
        fit = least_squares(lambda delta: residual(delta, train), np.zeros(3),
                            bounds=(-.01, .01), loss='huber', f_scale=.5, max_nfev=100)
        singular = np.linalg.svd(fit.jac, compute_uv=False)
        folds.append({'held_out_frame': held_frame, 'fit_succeeded': bool(fit.success),
                      'rotation_vector_degrees': np.rad2deg(fit.x).tolist(),
                      'jacobian_condition': float(singular[0]/max(singular[-1], 1e-15)),
                      'held_out_before_p90_pixels': float(np.quantile(abs(residual(np.zeros(3), held)), .9)),
                      'held_out_after_p90_pixels': float(np.quantile(abs(residual(fit.x, held)), .9))})
    return {'status': 'diagnostic_hypothesis_only', 'applied': False,
            'metric_accuracy_verified': False, 'factory_baseline_changed': False,
            'reference_height_used': False, 'folds': folds}
