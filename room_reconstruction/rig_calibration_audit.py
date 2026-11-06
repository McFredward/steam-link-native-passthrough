"""Diagnose a constant camera-rig/tracked-head mismatch without applying it.

Both eyes retain their factory relative geometry and metric baseline. A single
bounded rigid transform is fitted from temporal correspondences. Each validation
fold excludes both eyes of every held-out exposure from calibration fitting.
There is no time extrapolation, pose replacement or room-dimension prior.
"""
from __future__ import annotations

import copy
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation

from .measurement import unproject_kb, fit_track


def _distribution(values):
    values = np.asarray(values, dtype=np.float64)
    return {'count': len(values), 'median': float(np.median(values)),
            'p90': float(np.quantile(values, .9)), 'p95': float(np.quantile(values, .95))}


def _camera_factors(camera, pixels):
    local, valid = unproject_kb(pixels*camera.sample_step+camera.sample_offset, camera.intrinsics, camera.coefficients)
    optical_rotation, origin = camera.row_transforms(pixels)
    head_rotation = optical_rotation @ camera.optical[:, :3].T
    head_position = origin-np.einsum('nij,j->ni', head_rotation, camera.optical[:, 3])
    return (head_rotation, head_position, local, camera.optical), valid


def _residual(parameters, factors):
    correction = Rotation.from_rotvec(parameters[:3]).as_matrix()
    translation = parameters[3:]
    errors = []
    for _, _, a, b in factors:
        rays = []
        for head_rotation, head_position, local, optical in (a, b):
            direction = np.einsum('nij,nj->ni', head_rotation,
                                  local @ (correction @ optical[:, :3]).T)
            origin = head_position+np.einsum('nij,j->ni', head_rotation,
                                            correction @ optical[:, 3]+translation)
            rays.append((origin, direction))
        oa, da = rays[0]; ob, db = rays[1]
        plane = np.cross(ob-oa, da)
        sine = np.sum(plane*db, axis=1)/np.maximum(np.linalg.norm(plane, axis=1), 1e-12)
        # Radians avoid treating differing lens focal lengths as geometry.
        errors.append(np.arcsin(np.clip(sine, -1, 1)))
    return np.concatenate(errors)


def rig_hypothesis(cameras, pairs, *, descriptor_outlier_filter=True):
    """Return a cross-validated diagnostic, never a usable calibration grant.

    In production the correspondences are mutual descriptor matches. An
    independently estimated essential matrix removes gross descriptor outliers;
    it supplies neither poses nor metric scale to the fitted hypothesis. The
    analytic test path may supply known correspondences without that filter.
    """
    base = {'applied': False, 'metric_accuracy_verified': False,
            'factory_relative_stereo_changed': False, 'factory_baseline_changed': False,
            'reference_height_used': False, 'poses_extrapolated': False,
            'accepted_for_live_installation': False}
    if len(cameras) > 512 or len(pairs) > 256:
        raise ValueError('Rig-calibration dataset exceeds its explicit budget')
    factors = []
    for pair in pairs:
        a, b = cameras[pair['a']], cameras[pair['b']]
        if a.frame == b.frame or a.eye != b.eye:
            raise ValueError('Rig calibration needs temporal correspondences in the same eye')
        pa = np.asarray(pair['pixels_a'], dtype=np.float64)
        pb = np.asarray(pair['pixels_b'], dtype=np.float64)
        if pa.shape != pb.shape or pa.ndim != 2 or pa.shape[1] != 2 or len(pa) > 3000:
            raise ValueError('Invalid bounded rig-calibration correspondences')
        if len(pa) < 12:
            continue
        fa, va = _camera_factors(a, pa); fb, vb = _camera_factors(b, pb)
        usable = va & vb
        if descriptor_outlier_filter:
            import cv2
            # This global-shutter model is only a gross-outlier filter. All
            # subsequent residuals use the original exposure-row transforms.
            la, lb = fa[2], fb[2]
            _, mask = cv2.findEssentialMat(la[:, :2]/la[:, 2:], lb[:, :2]/lb[:, 2:],
                np.eye(3), method=cv2.RANSAC, prob=.999, threshold=.0015)
            if mask is None:
                continue
            usable &= mask[:, 0] != 0
        indices = np.flatnonzero(usable)
        if len(indices) < 12:
            continue
        # Bound fitting independently of how many features one display supplies.
        if len(indices) > 150:
            indices = indices[np.linspace(0, len(indices)-1, 150).astype(int)]
        selected = lambda f: tuple(x[indices] for x in f[:3])+(f[3],)
        factors.append((a.frame, b.frame, selected(fa), selected(fb)))
    frames = sorted({f for a, b, *_ in factors for f in (a, b)})
    if len(frames) < 10 or len(factors) < 8:
        return base | {'status': 'insufficient_independent_temporal_pairs', 'folds': []}
    folds = []
    bounds = np.r_[np.full(3, np.deg2rad(3)), np.full(3, .03)]
    for fold in range(5):
        held_frames = set(frames[fold::5])
        training = [f for f in factors if not held_frames.intersection(f[:2])]
        held = [f for f in factors if held_frames.intersection(f[:2])]
        if len(training) < 4 or len(held) < 2:
            return base | {'status': 'insufficient_disjoint_exposure_groups', 'folds': []}
        fit = least_squares(lambda p: _residual(p, training), np.zeros(6),
                            bounds=(-bounds, bounds), loss='huber',
                            f_scale=np.deg2rad(.08), max_nfev=100,
                            gtol=1e-12, ftol=1e-10, xtol=1e-10)
        singular = np.linalg.svd(fit.jac, compute_uv=False)
        full_rank = bool(singular[-1] > max(singular[0]*1e-6, 1e-9))
        condition = float(singular[0]/max(singular[-1], 1e-15))
        folds.append({'held_out_frames': sorted(held_frames),
                      'training_frames': sorted({f for a, b, *_ in training for f in (a, b)}),
                      'training_pairs': len(training), 'held_out_pairs': len(held),
                      'fit_succeeded': bool(fit.success),
                      'at_parameter_bound': bool(np.any(abs(fit.x) > bounds*.98)),
                      'full_rank': full_rank,
                      'jacobian_condition': condition,
                      'rotation_vector_degrees': np.rad2deg(fit.x[:3]).tolist(),
                      'translation_m': fit.x[3:].tolist(),
                      'held_out_before_degrees': _distribution(np.rad2deg(abs(_residual(np.zeros(6), held)))),
                      'held_out_after_degrees': _distribution(np.rad2deg(abs(_residual(fit.x, held))))})
    qualified = all(f['fit_succeeded'] and f['full_rank'] and not f['at_parameter_bound']
                    and f['jacobian_condition'] < 1000 for f in folds)
    return base | {'status': 'diagnostic_hypothesis_only' if qualified else 'unobservable_or_unbounded_hypothesis',
                   'folds': folds, 'temporal_pairs': len(factors),
                   'descriptor_outlier_filter': descriptor_outlier_filter,
                   'scope': 'Epipolar consistency only; no metric surface or live reconstruction acceptance'}


def prediction_comparison(cameras, tracks, hypothesis):
    """Test point predictions with calibration and point fits excluding the image.

    Corrected cameras exist only in this offline diagnostic. Compare errors on
    exactly the same tracks as well as each method's training-qualified set.
    Unknown or poorly observed tracks are counted, never filled with a plane.
    """
    def model_for_fold(fold):
        rotation = Rotation.from_rotvec(np.deg2rad(fold['rotation_vector_degrees'])).as_matrix()
        shift = np.asarray(fold['translation_m'])
        values = []
        for camera in cameras:
            value = copy.copy(camera)
            value.optical = np.column_stack((rotation @ camera.optical[:, :3],
                                             rotation @ camera.optical[:, 3]+shift))
            values.append(value)
        return values
    return _prediction_comparison(cameras, tracks, hypothesis, model_for_fold)


def _prediction_comparison(cameras, tracks, hypothesis, model_for_fold):
    base = {'applied_to_live_scanner': False, 'room_height_verified': False,
            'accepted_for_live_installation': False, 'factory_baseline_changed': False,
            'reference_height_used': False,
            'calibration_and_point_fit_exclude_both_eyes_of_prediction_exposure': True}
    if hypothesis['status'] != 'diagnostic_hypothesis_only':
        return base | {'status': 'no_observable_rig_hypothesis'}
    corrected, fold_for_frame = [], {}
    for fold in hypothesis['folds']:
        if set(fold['held_out_frames']).intersection(fold['training_frames']):
            raise ValueError('Rig fit reused a prediction exposure')
        values = model_for_fold(fold)
        for frame in fold['held_out_frames']:
            if frame in fold_for_frame:
                raise ValueError('Rig prediction groups overlap')
            fold_for_frame[frame] = len(corrected)
        corrected.append(values)
    results = {'original': [], 'rig_hypothesis': []}
    failures = {key: 0 for key in results}
    paired = {key: [] for key in results}
    paired_tracks, unobservable = 0, 0
    for track in tracks:
        frames = sorted({cameras[i].frame for i, _ in track})
        if len(frames) < 3:
            continue
        held_frame = frames[len(frames)//2]
        if held_frame not in fold_for_frame:
            unobservable += 1
            continue
        training = [(i, pixel) for i, pixel in track if cameras[i].frame != held_frame]
        held = [(i, pixel) for i, pixel in track if cameras[i].frame == held_frame]
        per_track = {}
        for name, model in (('original', cameras), ('rig_hypothesis', corrected[fold_for_frame[held_frame]])):
            result = fit_track(model, training)
            if result is None or np.quantile(result[1], .95) > 1.5:
                failures[name] += 1
                continue
            errors = [float(np.linalg.norm(model[i].project_at_observed_rows(result[0], [pixel])[0]-pixel))
                      for i, pixel in held]
            results[name].extend(errors)
            per_track[name] = errors
        if len(per_track) == 2:
            paired_tracks += 1
            for name in paired:
                paired[name].extend(per_track[name])
    stats = lambda values: _distribution(values) if values else {'count': 0, 'median': None, 'p90': None, 'p95': None}
    return base | {'status': 'diagnostic_prediction_comparison',
                   'all_training_qualified_tracks': {key: stats(value) for key, value in results.items()},
                   'paired_same_tracks': {key: stats(value) for key, value in paired.items()},
                   'paired_track_count': paired_tracks, 'training_point_failures': failures,
                   'tracks_without_independent_rig_fit': unobservable}
