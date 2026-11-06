"""Exposure-disjoint joint stereo/head calibration diagnostic, never a live grant.

The measured factory baseline and both camera centers relative to the rig stay
fixed. A common rigid head/rig correction and a small relative optical rotation
are estimated from stereo and temporal correspondences. Known room dimensions
and reference geometry are not inputs. Actual historical row poses are retained.
"""
from __future__ import annotations

import copy
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation

from .live_geometry import matrix
from .rig_calibration_audit import _camera_factors, _distribution, _prediction_comparison

PARAMETER_BOUNDS = np.r_[np.full(3, np.deg2rad(3)), np.full(3, .03), np.full(3, np.deg2rad(1))]


def prediction_comparison(cameras, tracks, hypothesis):
    return _prediction_comparison(cameras, tracks, hypothesis,
                                 lambda fold: corrected_cameras(cameras, fold['parameters']))


def corrected_cameras(cameras, parameters):
    """Make private diagnostic copies, preserving observations and source proof."""
    parameters = np.asarray(parameters, dtype=np.float64)
    if (parameters.shape != (9,) or not np.isfinite(parameters).all()
            or np.any(abs(parameters) > PARAMETER_BOUNDS)):
        raise ValueError('Invalid joint calibration hypothesis')
    common = Rotation.from_rotvec(parameters[:3]).as_matrix()
    relative = Rotation.from_rotvec(parameters[6:]).as_matrix()
    result = []
    for camera in cameras:
        value = copy.copy(camera)
        optical = common @ camera.optical[:, :3]
        if camera.eye == 0:
            optical = optical @ relative
        value.optical = np.column_stack((optical, common @ camera.optical[:, 3]+parameters[3:6]))
        result.append(value)
    return result


def _residual(parameters, factors):
    common = Rotation.from_rotvec(parameters[:3]).as_matrix()
    relative = Rotation.from_rotvec(parameters[6:]).as_matrix()
    values = []
    for _, _, eye_a, eye_b, a, b in factors:
        rays = []
        for eye, (head_rotation, head_position, local, optical) in ((eye_a, a), (eye_b, b)):
            rotation = common @ optical[:, :3]
            if eye == 0:
                rotation = rotation @ relative
            direction = np.einsum('nij,nj->ni', head_rotation, local @ rotation.T)
            origin = head_position+np.einsum('nij,j->ni', head_rotation, common @ optical[:, 3]+parameters[3:6])
            rays.append((origin, direction))
        oa, da = rays[0]; ob, db = rays[1]
        plane = np.cross(ob-oa, da)
        sine = np.sum(plane*db, axis=-1)/np.maximum(np.linalg.norm(plane, axis=-1), 1e-12)
        values.append(np.arcsin(np.clip(sine, -1, 1)))
    return np.concatenate(values)


def _stratified(indices, pixels, extent, maximum=150):
    # Repetitive features on one display must not consume the complete budget.
    # Selection is deterministic, spatial, and independent of fitted residuals.
    cells = np.clip((pixels[indices]*4/extent).astype(int), 0, 3)
    labels = cells[:, 1]*4+cells[:, 0]
    groups = []
    for label in np.unique(labels):
        group = indices[labels == label]
        if len(group) > 12:
            group = group[np.linspace(0, len(group)-1, 12).astype(int)]
        groups.append(group)
    selected = [group[level] for level in range(12) for group in groups if level < len(group)]
    return np.asarray(selected[:maximum], dtype=int)


def joint_hypothesis(cameras, pairs, *, descriptor_outlier_filter=True):
    base = {'applied': False, 'factory_baseline_changed': False, 'camera_center_geometry_changed': False,
            'factory_relative_stereo_changed': True,
            'reference_height_used': False, 'poses_extrapolated': False,
            'accepted_for_live_installation': False, 'metric_accuracy_verified': False,
            'relative_optical_rotation_is_diagnostic_only': True}
    if len(cameras) > 512 or len(pairs) > 512:
        raise ValueError('Joint calibration dataset exceeds its explicit budget')
    factors = []
    for pair in pairs:
        a, b = cameras[pair['a']], cameras[pair['b']]
        pa, pb = np.asarray(pair['pixels_a']), np.asarray(pair['pixels_b'])
        if (pa.shape != pb.shape or pa.ndim != 2 or pa.shape[1] != 2 or len(pa) > 3000
                or not ((a.frame == b.frame and a.eye != b.eye) or (a.frame != b.frame and a.eye == b.eye))):
            raise ValueError('Invalid joint stereo/temporal correspondences')
        if len(pa) < 12:
            continue
        fa, va = _camera_factors(a, pa); fb, vb = _camera_factors(b, pb)
        usable = va & vb
        if descriptor_outlier_filter:
            import cv2
            deskewed = []
            for camera, geometry in ((a, fa), (b, fb)):
                rotations, _, local, optical = geometry
                world = np.einsum('nij,nj->ni', rotations, local @ optical[:, :3].T)
                reference = matrix(camera.observation['reference_head'])[:, :3]
                direction = world @ reference @ optical[:, :3]
                good = abs(direction[:, 2]) > 1e-6
                usable &= good
                normalized = np.zeros((len(direction), 2))
                np.divide(direction[:, :2], direction[:, 2:], out=normalized, where=good[:, None])
                deskewed.append(normalized)
            # Only a gross-outlier filter. Unlike a global-shutter shortcut,
            # these directions include each feature's actual historical row pose.
            valid_indices = np.flatnonzero(usable)
            if len(valid_indices) < 12:
                continue
            _, mask = cv2.findEssentialMat(*(d[valid_indices] for d in deskewed), np.eye(3), method=cv2.RANSAC,
                                           prob=.999, threshold=.0015)
            if mask is None:
                continue
            usable[:] = False
            usable[valid_indices] = mask[:, 0] != 0
        indices = _stratified(np.flatnonzero(usable), pa, a.extent)
        if len(indices) < 12:
            continue
        selected = lambda g: tuple(v[indices] for v in g[:3])+(g[3],)
        factors.append((a.frame, b.frame, a.eye, b.eye, selected(fa), selected(fb)))
    frames = sorted({f for a, b, *_ in factors for f in (a, b)})
    if len(frames) < 10:
        return base | {'status': 'insufficient_independent_exposures', 'folds': []}
    bounds = PARAMETER_BOUNDS
    folds = []
    for fold in range(5):
        held_frames = set(frames[fold::5])
        training = [p for p in factors if not held_frames.intersection(p[:2])]
        held = [p for p in factors if held_frames.intersection(p[:2])]
        if (sum(p[0] == p[1] for p in training) < 3 or sum(p[0] != p[1] for p in training) < 4
                or len(held) < 2):
            return base | {'status': 'insufficient_disjoint_stereo_temporal_groups', 'folds': []}
        fit = least_squares(lambda p: _residual(p, training), np.zeros(9), bounds=(-bounds, bounds),
                            loss='huber', f_scale=np.deg2rad(.08), max_nfev=100,
                            gtol=1e-12, ftol=1e-10, xtol=1e-10)
        singular = np.linalg.svd(fit.jac, compute_uv=False)
        folds.append({'held_out_frames': sorted(held_frames),
                      'training_frames': sorted({f for a, b, *_ in training for f in (a, b)}),
                      'parameters': fit.x.tolist(), 'fit_succeeded': bool(fit.success),
                      'full_rank': bool(singular[-1] > max(singular[0]*1e-6, 1e-9)),
                      'jacobian_condition': float(singular[0]/max(singular[-1], 1e-15)),
                      'at_parameter_bound': bool(np.any(abs(fit.x) > bounds*.98)),
                      'common_rotation_vector_degrees': np.rad2deg(fit.x[:3]).tolist(),
                      'translation_m': fit.x[3:6].tolist(),
                      'left_local_rotation_vector_degrees': np.rad2deg(fit.x[6:]).tolist(),
                      'held_out_before_degrees': _distribution(np.rad2deg(abs(_residual(np.zeros(9), held)))),
                      'held_out_after_degrees': _distribution(np.rad2deg(abs(_residual(fit.x, held))))})
    qualified = all(f['fit_succeeded'] and f['full_rank'] and not f['at_parameter_bound']
                    and f['jacobian_condition'] < 1000 for f in folds)
    return base | {'status': 'diagnostic_hypothesis_only' if qualified else 'unobservable_or_unbounded_hypothesis',
                   'folds': folds, 'stereo_pairs': sum(p[0] == p[1] for p in factors),
                   'temporal_pairs': sum(p[0] != p[1] for p in factors),
                   'spatial_feature_stratification': True, 'historical_row_deskew_for_outlier_filter': True}
