"""Private, fixed-camera multi-view audit. Never installs or starts capture.

Run with the numerical developer environment. Reports are deliberately allowed
to fail: the reference room height is evaluated only after reconstruction.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import time

import numpy as np

from .measurement import observation_cameras, epipolar_error_degrees, fit_track, measured_room_height
from .calibration_audit import rotation_hypothesis
from .rig_calibration_audit import rig_hypothesis, prediction_comparison


def load_observations(path, maximum=256):
    observations = []
    with Path(path).open('r', encoding='utf-8') as source:
        while True:
            line = source.readline(1100001)
            if not line:
                break
            if len(line) > 1100000 or len(observations) >= maximum:
                raise ValueError('Measurement dataset exceeds its explicit budget')
            value = json.loads(line)
            if observations:
                previous = observations[-1]
                if (value['binding'] != previous['binding'] or value['source_session'] != previous['source_session']
                        or value['sequence'] <= previous['sequence'] or value['exposure_ticks'] <= previous['exposure_ticks']):
                    raise ValueError('Measurement source, origin or exposure sequence changed')
            observations.append(value)
    if len(observations) < 3:
        raise ValueError('At least three distinct stereo observations are required')
    return observations


def distribution(values):
    values = np.asarray(values, dtype=np.float64)
    values = values[np.isfinite(values)]
    if not len(values):
        return {'count': 0, 'median': None, 'p90': None, 'p95': None}
    return {'count': len(values), 'median': float(np.median(values)),
            'p90': float(np.quantile(values, .9)), 'p95': float(np.quantile(values, .95))}


def build_tracks(cameras, maximum_features=3000):
    import cv2
    sift = cv2.SIFT_create(nfeatures=maximum_features, contrastThreshold=.008)
    features = [sift.detectAndCompute(camera.image, None) for camera in cameras]
    matcher = cv2.BFMatcher()
    parents = {}

    def root(node):
        parents.setdefault(node, node)
        while parents[node] != node:
            parents[node] = parents[parents[node]]
            node = parents[node]
        return node

    summaries, stereo_measurements, temporal_measurements = [], [], []
    for a in range(len(cameras)):
        for b in range(a+1, len(cameras)):
            # Bounded temporal window, plus every stereo pair. A full growing
            # history must not turn one new observation into quadratic work.
            if abs(cameras[a].frame-cameras[b].frame) > 3:
                continue
            ka, da = features[a]; kb, db = features[b]
            if da is None or db is None or min(len(da), len(db)) < 2:
                continue
            forward = [(m.queryIdx, m.trainIdx) for pair in matcher.knnMatch(da, db, k=2)
                       if len(pair) == 2 for m, n in [pair] if m.distance < .72*n.distance]
            reverse = {(m.trainIdx, m.queryIdx) for pair in matcher.knnMatch(db, da, k=2)
                       if len(pair) == 2 for m, n in [pair] if m.distance < .72*n.distance}
            matches = [(i, j) for i, j in forward if (i, j) in reverse]
            if not matches:
                continue
            pa = np.asarray([ka[i].pt for i, _ in matches])
            pb = np.asarray([kb[j].pt for _, j in matches])
            error = epipolar_error_degrees(cameras[a], cameras[b], pa, pb)
            if cameras[a].frame == cameras[b].frame:
                stereo_measurements.append({'a': a, 'b': b, 'pixels_a': pa, 'pixels_b': pb})
            elif (cameras[a].eye == cameras[b].eye == 0
                  and cameras[b].frame-cameras[a].frame == 1):
                temporal_measurements.append({'a': a, 'b': b, 'pixels_a': pa, 'pixels_b': pb})
            summaries.append({'frames': [cameras[a].frame, cameras[b].frame],
                              'stereo': cameras[a].frame == cameras[b].frame,
                              'mutual_matches': len(matches), 'epipolar_error_degrees': distribution(error)})
            # This excludes gross mismatches for building tracks. The audit
            # above retains all descriptor matches, before the geometry gate.
            for (i, j), e in zip(matches, error):
                if np.isfinite(e) and e < .2:
                    ra, rb = root((a, i)), root((b, j))
                    if ra != rb:
                        parents[rb] = ra
    groups = {}
    for node in list(parents):
        groups.setdefault(root(node), []).append(node)
    tracks, conflicts = [], 0
    for nodes in groups.values():
        camera_ids = [node[0] for node in nodes]
        if len(camera_ids) != len(set(camera_ids)):
            conflicts += 1
            continue
        if len(nodes) < 3 or len({cameras[i].frame for i in camera_ids}) < 2:
            continue
        tracks.append([(i, np.asarray(features[i][0][j].pt)) for i, j in sorted(nodes)])
    return tracks, summaries, conflicts, stereo_measurements, temporal_measurements


def fit_tracks(cameras, tracks):
    """Score unseen exposures before any selection that could see their errors."""
    points, fit_errors, held_out = [], [], []
    training_accepted, prediction_failed = 0, 0
    for track in tracks:
        frames = sorted({cameras[i].frame for i, _ in track})
        if len(frames) >= 3:
            # Neither eye of this exposure participates in point fitting or
            # in the training-only residual gate. Bad unseen predictions stay
            # in the score even if the later all-view point fit rejects them.
            held_frame = frames[len(frames)//2]
            training = [(i, p) for i, p in track if cameras[i].frame != held_frame]
            predicted = fit_track(cameras, training)
            if predicted is not None and np.quantile(predicted[1], .95) <= 1.5:
                training_accepted += 1
                for i, pixel in track:
                    if cameras[i].frame == held_frame:
                        pixel_prediction = cameras[i].project_at_observed_rows(predicted[0], [pixel])[0]
                        held_out.append(float(np.linalg.norm(pixel_prediction-pixel)))
            else:
                prediction_failed += 1
        result = fit_track(cameras, track)
        if result is None:
            continue
        point, errors = result
        fit_errors.extend(errors.tolist())
        if np.quantile(errors, .95) > 1.5:
            continue
        points.append(point)
    return np.asarray(points).reshape(-1, 3), fit_errors, held_out, training_accepted, prediction_failed


def audit(observations, reference_height_m=None, *, compare_rig_predictions=False):
    started = time.perf_counter()
    cameras = [camera for i, observation in enumerate(observations)
               for camera in observation_cameras(observation, i)]
    tracks, pairs, conflicts, stereo_measurements, temporal_measurements = build_tracks(cameras)
    points, fit_errors, held_out, training_accepted, prediction_failed = fit_tracks(cameras, tracks)
    height = measured_room_height(points)
    positions = np.asarray([np.asarray(o['reference_head']).reshape(3, 4)[:, 3] for o in observations])
    span = float(np.linalg.norm(positions[:, None]-positions[None], axis=-1).max())
    stereo = [p['epipolar_error_degrees']['p90'] for p in pairs if p['stereo']]
    stereo = [v for v in stereo if v is not None]
    holdout = distribution(held_out)
    reasons = []
    if len(points) < 30:
        reasons.append('insufficient_multi_view_tracks')
    if span < .15:
        reasons.append('insufficient_camera_translation_for_independent_validation')
    if not stereo or max(stereo) > .1:
        reasons.append('stereo_calibration_residual_needs_investigation')
    if holdout['count'] < 40 or holdout['p95'] is None or holdout['p95'] > 1:
        reasons.append('held_out_reprojection_not_verified')
    error = None
    if reference_height_m is None:
        reasons.append('missing_independent_room_height')
    elif height['height_m'] is None:
        reasons.append(height['reason'])
    else:
        error = abs(height['height_m']-reference_height_m)
        if error > .05:
            reasons.append('room_height_error_exceeds_five_centimetres')
    report = {'schema': 'sfnp-room-measurement-audit-v1', 'accepted': not reasons,
              'reasons': reasons, 'frames': len(observations), 'camera_views': len(cameras),
              'temporal_span_seconds': (observations[-1]['exposure_ticks']-observations[0]['exposure_ticks'])*1e-9,
              'head_translation_span_m': span, 'matching_tracks': len(tracks), 'conflicting_tracks': conflicts,
              'joint_metric_points': len(points), 'fit_reprojection_pixels': distribution(fit_errors),
              'held_out_reprojection_pixels': holdout, 'pair_geometry': pairs,
              'held_out_training_accepted_tracks': training_accepted,
              'held_out_point_prediction_failures': prediction_failed,
              'held_out_selection_uses_training_only': True,
              'room_height': height, 'independent_reference_height_m': reference_height_m,
              'room_height_absolute_error_m': error, 'audit_wall_seconds': time.perf_counter()-started,
              'scope': 'offline measurement audit; not live performance or installed scanner acceptance',
              'stereo_rotation_hypothesis': rotation_hypothesis(cameras, stereo_measurements),
              'tracked_head_rig_hypothesis': rig_hypothesis(cameras, temporal_measurements),
              'accepted_for_live_installation': False, 'live_latency_verified': False,
              'poses_or_calibration_adjusted': False, 'reference_used_as_reconstruction_prior': False}
    if compare_rig_predictions:
        report['tracked_head_rig_prediction_comparison'] = prediction_comparison(
            cameras, tracks, report['tracked_head_rig_hypothesis'])
    report['audit_wall_seconds'] = time.perf_counter()-started
    return report, points


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('observations', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='New private output directory')
    parser.add_argument('--reference-room-height', type=float)
    parser.add_argument('--compare-rig-predictions', action='store_true',
                        help='Also evaluate the offline rig hypothesis on unseen point projections')
    args = parser.parse_args()
    if args.reference_room_height is not None and not 1.8 <= args.reference_room_height <= 5:
        parser.error('Reference room height must be in metres, between 1.8 and 5')
    if args.output.exists():
        parser.error('Choose a new private output directory; existing evidence is preserved')
    observations = load_observations(args.observations)
    report, points = audit(observations, args.reference_room_height,
                          compare_rig_predictions=args.compare_rig_predictions)
    report['input_sha256'] = hashlib.sha256(args.observations.read_bytes()).hexdigest()
    args.output.mkdir(mode=0o700)
    (args.output/'report.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    np.savez_compressed(args.output/'private-metric-points.npz', points=points)
    print(json.dumps({key: report[key] for key in ('accepted', 'reasons', 'frames', 'joint_metric_points',
                                                  'held_out_reprojection_pixels', 'room_height_absolute_error_m')}))


if __name__ == '__main__':
    main()
