"""Independent metric and exposure-row checks for the reconstruction rebuild."""
import base64
import copy
import unittest

import numpy as np
from scipy.spatial.transform import Rotation

from room_reconstruction.measurement import (
    observation_cameras, fit_track, intersect_rays, measured_room_height, unproject_kb,
)
from room_reconstruction.live_geometry import project_kb
from room_reconstruction.calibration_audit import rotation_hypothesis
from room_reconstruction.rig_calibration_audit import rig_hypothesis, prediction_comparison
from room_reconstruction.mesh_metrics import box_metrics
from room_reconstruction.measurement_audit import audit, fit_tracks


def observation(offset=0., yaw=0., moving=False, epoch_ns=1000000000):
    first = epoch_ns
    last = first+2463*4360
    rotation = Rotation.from_euler('y', yaw).as_matrix()
    transform = lambda r, t: np.column_stack((r, t)).ravel().tolist()
    def head(t):
        r = rotation @ Rotation.from_euler('x', t*.005 if moving else 0.).as_matrix()
        return transform(r, (offset+t*.001 if moving else offset, 1.5, 0))
    image = base64.b64encode(bytes([116])*(616*616)).decode()
    optical = np.diag((1., -1., -1.))
    return {'schema': 'sfnp-room-stereo-v1', 'encoding': 'gray8-point-step4',
            'width': 616, 'height': 616, 'images': [image, image], 'reference_head': head(.5),
            'exposure_ticks': first+(last-first)//2,
            'timing': [{'first_row_ns': first, 'line_ns': 4360, 'integration_ns': 1000000} for _ in range(2)],
            'poses': [{'raw_ns': first+(last-first)*i//8, 'query_ns': 100, 'head': head(i/8)} for i in range(9)],
            'rig': {'baseline_m': .064, 'head_from_rectified': transform(np.eye(3), (-.032, 0, 0)),
                    'eyes': [{'intrinsics': [1000., 1000., 1232., 1232.], 'k': [0., 0., 0., 0.],
                              'head_from_optical': transform(optical, (x, 0, 0))} for x in (-.032, .032)]}}


def project(camera, point):
    pixel = np.array((308., 308.))
    for _ in range(8):
        pixel = camera.project_at_observed_rows(point, [pixel])[0]
    return pixel


class MeasurementTests(unittest.TestCase):
    def test_factory_kb_inverse_retains_metric_ray_and_point_sampling_convention(self):
        k = np.array((.03, -.01, .001, -.0001)); intrinsics = np.array((1460., 1470., 1100., 1240.))
        rays = np.array(((0, 0, 1), (.5, -.2, 1), (-.7, .4, 1)), float)
        pixels = project_kb(rays, intrinsics, k)
        measured, valid = unproject_kb(pixels, intrinsics, k)
        self.assertTrue(valid.all())
        self.assertTrue(np.allclose(measured, rays/np.linalg.norm(rays, axis=1)[:, None], atol=1e-11))

    def test_multi_view_fit_recovers_known_world_point_while_head_and_rows_move(self):
        cameras = [camera for i, offset in enumerate((-.12, 0, .12))
                   for camera in observation_cameras(observation(offset, yaw=offset*.3, moving=True), i)]
        expected = np.array((.2, 1.9, -2.4))
        measurements = [(i, project(camera, expected)) for i, camera in enumerate(cameras)]
        point, error = fit_track(cameras, measurements)
        self.assertLess(float(np.linalg.norm(point-expected)), 1e-8)
        self.assertLess(float(error.max()), 1e-7)
        # A distinct exposure is evaluated without including either of its
        # images in the fit that predicts it.
        training = [m for m in measurements if cameras[m[0]].frame != 1]
        point, _ = fit_track(cameras, training)
        for i, pixel in measurements:
            if cameras[i].frame == 1:
                self.assertLess(float(np.linalg.norm(cameras[i].project_at_observed_rows(point, [pixel])[0]-pixel)), 1e-7)

    def test_sampled_pixel_uses_its_own_exposure_pose_against_analytic_motion(self):
        camera=observation_cameras(observation(.1,yaw=.03,moving=True),0)[0]
        pixels=np.array(((150.,50.),(300.,500.)))
        origin,rays,valid=camera.rays(pixels)
        self.assertTrue(valid.all())
        for i,pixel in enumerate(pixels):
            t=np.floor(pixel[1]*4+.5)/2463
            rotation=Rotation.from_euler('y',.03).as_matrix() @ Rotation.from_euler('x',t*.005).as_matrix()
            xy=(pixel*4-1232)/1000;theta=np.linalg.norm(xy)
            optical_ray=np.r_[xy*np.sin(theta)/theta,np.cos(theta)]
            expected_origin=rotation @ np.array((-.032,0,0))+np.array((.1+t*.001,1.5,0))
            expected_ray=rotation @ np.diag((1.,-1.,-1.)) @ optical_ray
            self.assertTrue(np.allclose(origin[i],expected_origin,atol=1e-9,rtol=0))
            self.assertTrue(np.allclose(rays[i],expected_ray,atol=1e-8,rtol=0))

    def test_failed_unseen_predictions_are_not_hidden_by_an_all_view_inlier_gate(self):
        cameras = [c for i, x in enumerate((-.15, 0, .15))
                   for c in observation_cameras(observation(x), i)]
        point = np.array((.1, 1.7, -2.2))
        track = [(i, project(c, point)+(np.array((0., 7.)) if c.frame == 1 else 0.))
                 for i, c in enumerate(cameras)]
        points, _, errors, accepted, failed = fit_tracks(cameras, [track])
        self.assertEqual(len(points), 0)
        self.assertEqual(accepted, 1); self.assertEqual(failed, 0)
        self.assertEqual(len(errors), 2)
        self.assertTrue(np.allclose(errors, 7., atol=1e-8))

    def test_missing_parallax_and_points_behind_cameras_are_not_metric_measurements(self):
        self.assertIsNone(intersect_rays(((0, 0, 0), (.1, 0, 0)), ((0, 0, -1), (0, 0, -1))))
        self.assertIsNone(intersect_rays(((-.1, 0, 0), (.1, 0, 0)), ((-.1, 0, -1), (.1, 0, -1))))

    def test_claimed_baseline_and_historical_pose_admission_stay_mandatory(self):
        wrong = observation(); wrong['rig']['baseline_m'] = .09
        self.assertRaisesRegex(ValueError, 'baseline', observation_cameras, wrong, 0)
        wrong = observation(); wrong['poses'][-1]['raw_ns'] -= 100000
        self.assertRaises(ValueError, observation_cameras, wrong, 0)
        wrong = observation(); wrong['timing'][0]['uncertainty_ns'] = 3000000
        self.assertRaises(ValueError, observation_cameras, wrong, 0)

    def test_independent_height_detects_a_biased_ceiling_and_is_invariant_to_origin(self):
        x, z = np.mgrid[-1:1:9j, -1:1:9j]
        floor = np.column_stack((x.ravel(), np.full(x.size, 7.3), z.ravel()))
        ceiling = floor.copy(); ceiling[:, 1] += 2.535
        result = measured_room_height(np.concatenate((floor, ceiling)))
        self.assertAlmostEqual(result['height_m'], 2.535)
        ceiling[:, 1] += .12
        biased = measured_room_height(np.concatenate((floor, ceiling)))
        self.assertAlmostEqual(abs(biased['height_m']-2.535), .12)
        self.assertIsNone(measured_room_height(ceiling)['height_m'])

    def test_two_lines_do_not_prove_floor_and_ceiling_surfaces(self):
        x = np.linspace(-1, 1, 100)
        line = np.column_stack((x, np.zeros_like(x), x))
        other = line.copy(); other[:, 1] = 2.535
        self.assertIsNone(measured_room_height(np.concatenate((line, other)))['height_m'])

    def test_horizontal_slices_through_vertical_walls_do_not_prove_room_planes(self):
        x,y=np.mgrid[-2:2:30j,0:2.7:30j]
        wall_a=np.column_stack((x.ravel(),y.ravel(),np.full(x.size,-2.)))
        wall_b=wall_a.copy();wall_b[:,2]=2.
        z,y=np.mgrid[-2:2:30j,0:2.7:30j]
        wall_c=np.column_stack((np.full(z.size,-2.),y.ravel(),z.ravel()))
        wall_d=wall_c.copy();wall_d[:,0]=2.
        self.assertIsNone(measured_room_height(np.concatenate((wall_a,wall_b,wall_c,wall_d)))['height_m'])

    def test_calibration_hypothesis_predicts_unseen_exposures_without_applying_a_change(self):
        cameras = [camera for i, offset in enumerate((-.12, 0, .12))
                   for camera in observation_cameras(observation(offset, yaw=offset*.3), i)]
        before = [camera.optical.copy() for camera in cameras]
        delta = np.array((.001, -.002, .0007))
        rng = np.random.default_rng(837)
        points = rng.uniform((-.6,.8,-3),(.6,2.2,-1.5),(80,3))
        pairs = []
        for i in range(0, len(cameras), 2):
            truth = copy.copy(cameras[i+1]); truth.optical = truth.optical.copy()
            truth.optical[:, :3] = truth.optical[:, :3] @ Rotation.from_rotvec(delta).as_matrix()
            pairs.append({'a':i,'b':i+1,'pixels_a':np.array([project(cameras[i],p) for p in points]),
                          'pixels_b':np.array([project(truth,p) for p in points])})
        result = rotation_hypothesis(cameras, pairs)
        self.assertFalse(result['applied']); self.assertFalse(result['metric_accuracy_verified'])
        for fold in result['folds']:
            self.assertLess(fold['held_out_after_p90_pixels'],1e-6)
            self.assertTrue(np.allclose(np.deg2rad(fold['rotation_vector_degrees']),delta,atol=1e-7))
        for camera, previous in zip(cameras,before):
            self.assertTrue(np.array_equal(camera.optical,previous))

    def test_constant_rig_mismatch_is_identified_from_disjoint_exposures_without_changing_cameras(self):
        correction = Rotation.from_rotvec((.023, .002, -.02)).as_matrix()
        shift = np.array((.006, .003, -.01))
        rng = np.random.default_rng(1138)
        points = rng.uniform((-.5, 1., -3.), (.5, 2., -1.8), (45, 3))
        cameras, truth = [], []
        for frame in range(15):
            value = observation(moving=True, epoch_ns=1000000000+frame*100000000)
            phase = frame*.45
            rotation = Rotation.from_euler('xyz', (.12*np.sin(phase), .18*np.cos(phase), .1*np.sin(2*phase))).as_matrix()
            position = np.array((.2*np.sin(phase), .06*np.cos(2*phase), .12*np.sin(2*phase)))
            for sample in value['poses']:
                head = np.array(sample['head']).reshape(3, 4)
                head[:, :3] = rotation @ head[:, :3]
                head[:, 3] += position
                sample['head'] = head.ravel().tolist()
            head = np.array(value['reference_head']).reshape(3, 4)
            head[:, :3] = rotation @ head[:, :3]; head[:, 3] += position
            value['reference_head'] = head.ravel().tolist()
            for camera in observation_cameras(value, frame):
                expected = copy.copy(camera); expected.optical = expected.optical.copy()
                expected.optical[:, :3] = correction @ expected.optical[:, :3]
                expected.optical[:, 3] = correction @ expected.optical[:, 3]+shift
                cameras.append(camera)
                pixels = np.full((len(points), 2), 308.)
                for _ in range(8):
                    pixels = expected.project_at_observed_rows(points, pixels)
                truth.append(pixels)
        previous = [camera.optical.copy() for camera in cameras]
        pairs = [{'a': i, 'b': i+2, 'pixels_a': truth[i], 'pixels_b': truth[i+2]} for i in range(0, 28, 2)]
        result = rig_hypothesis(cameras, pairs, descriptor_outlier_filter=False)
        self.assertEqual(result['status'], 'diagnostic_hypothesis_only')
        self.assertFalse(result['applied']); self.assertFalse(result['accepted_for_live_installation'])
        self.assertFalse(result['factory_baseline_changed']); self.assertFalse(result['poses_extrapolated'])
        for fold in result['folds']:
            self.assertTrue(set(fold['held_out_frames']).isdisjoint(fold['training_frames']))
            self.assertTrue(fold['full_rank']); self.assertFalse(fold['at_parameter_bound'])
            self.assertLess(fold['held_out_after_degrees']['p95'], 1e-5)
            self.assertTrue(np.allclose(np.deg2rad(fold['rotation_vector_degrees']), (.023, .002, -.02), atol=1e-6))
            self.assertTrue(np.allclose(fold['translation_m'], shift, atol=1e-6))
        for camera, before in zip(cameras, previous):
            self.assertTrue(np.array_equal(camera.optical, before))
        tracks = [[(i, truth[i][j]) for i in range(len(cameras))] for j in range(len(points))]
        prediction = prediction_comparison(cameras, tracks, result)
        self.assertEqual(prediction['status'], 'diagnostic_prediction_comparison')
        self.assertFalse(prediction['accepted_for_live_installation'])
        self.assertGreater(prediction['all_training_qualified_tracks']['original']['median'], .1)
        self.assertLess(prediction['all_training_qualified_tracks']['rig_hypothesis']['p95'], 1e-6)
        for camera, before in zip(cameras, previous):
            self.assertTrue(np.array_equal(camera.optical, before))
        bad = copy.deepcopy(result)
        bad['folds'][0]['training_frames'] += bad['folds'][0]['held_out_frames']
        self.assertRaisesRegex(ValueError, 'reused', prediction_comparison, cameras, tracks, bad)
        filtered = rig_hypothesis(cameras, pairs)
        self.assertEqual(filtered['status'], 'diagnostic_hypothesis_only')
        for fold in filtered['folds']:
            self.assertLess(fold['held_out_after_degrees']['p95'], 1e-5)
        # Gross descriptor outliers can bias a global-shutter outlier filter.
        # A lower held-out residual must never override parameter-bound refusal.
        noisy = copy.deepcopy(pairs)
        for pair in noisy:
            pair['pixels_a'] = np.concatenate((pair['pixels_a'], rng.uniform(210., 400., (8, 2))))
            pair['pixels_b'] = np.concatenate((pair['pixels_b'], rng.uniform(210., 400., (8, 2))))
        filtered = rig_hypothesis(cameras, noisy)
        self.assertFalse(filtered['applied']); self.assertFalse(filtered['accepted_for_live_installation'])
        if any(f['at_parameter_bound'] for f in filtered['folds']):
            self.assertEqual(filtered['status'], 'unobservable_or_unbounded_hypothesis')
        for fold in filtered['folds']:
            self.assertTrue(set(fold['held_out_frames']).isdisjoint(fold['training_frames']))

    def test_stationary_rig_hypothesis_is_unobservable_and_short_sequences_cannot_calibrate(self):
        cameras = [observation_cameras(observation(), i)[0] for i in range(12)]
        points = np.array([(x, y, -2.) for x in (-.3, 0, .3) for y in (1.1, 1.3, 1.5, 1.7, 1.9)])
        pixels = np.array([project(cameras[0], p) for p in points])
        pairs = [{'a': i, 'b': i+1, 'pixels_a': pixels, 'pixels_b': pixels} for i in range(11)]
        result = rig_hypothesis(cameras, pairs, descriptor_outlier_filter=False)
        self.assertEqual(result['status'], 'unobservable_or_unbounded_hypothesis')
        self.assertTrue(all(not f['full_rank'] for f in result['folds']))
        self.assertEqual(prediction_comparison(cameras, [], result)['status'], 'no_observable_rig_hypothesis')
        self.assertEqual(rig_hypothesis(cameras[:3], pairs[:2], descriptor_outlier_filter=False)['status'],
                         'insufficient_independent_temporal_pairs')

    def test_rig_hypothesis_rejects_stereo_pairs_and_unbounded_feature_inputs(self):
        cameras = observation_cameras(observation(), 0)
        pixels = np.full((12, 2), 308.)
        self.assertRaisesRegex(ValueError, 'temporal', rig_hypothesis, cameras,
                               [{'a': 0, 'b': 1, 'pixels_a': pixels, 'pixels_b': pixels}])
        camera = observation_cameras(observation(), 1)[0]
        self.assertRaisesRegex(ValueError, 'bounded', rig_hypothesis, [cameras[0], camera],
                               [{'a': 0, 'b': 1, 'pixels_a': np.zeros((3001, 2)), 'pixels_b': np.zeros((3001, 2))}])

    def test_geometry_score_detects_surface_bias_and_geometry_outside_reference_bounds(self):
        vertices=np.array(((-1,0,-1),(1,0,-1),(1,0,1),(-1,0,1)),float)
        triangles=np.array(((0,1,2),(0,2,3)))
        x,z=np.mgrid[-.9:.9:9j,-.9:.9:9j]
        truth=np.column_stack((x.ravel(),np.zeros(x.size),z.ravel()))
        correct=box_metrics(vertices,triangles,truth,2.7)
        self.assertAlmostEqual(correct['known_surface_distance_p95_m'],0)
        self.assertEqual(correct['observed_surface_completeness_within_8cm'],1)
        biased=vertices.copy();biased[:,1]+=.12
        wrong=box_metrics(biased,triangles,truth,2.7)
        self.assertAlmostEqual(wrong['known_surface_distance_p95_m'],.12)
        self.assertEqual(wrong['observed_surface_completeness_within_8cm'],0)
        outside=vertices.copy();outside[:,0]+=10
        self.assertGreater(box_metrics(outside,triangles,truth,2.7)['known_surface_distance_p95_m'],7)

    def test_blank_observations_and_a_known_height_do_not_manufacture_a_room(self):
        observations=[observation(offset,epoch_ns=1000000000+i*100000000) for i,offset in enumerate((-.15,0,.15))]
        report,points=audit(observations,2.535)
        self.assertFalse(report['accepted']);self.assertFalse(report['accepted_for_live_installation'])
        self.assertEqual(len(points),0)
        self.assertIsNone(report['room_height_absolute_error_m'])
        self.assertIsNone(report['room_height']['height_m'])
        self.assertFalse(report['reference_used_as_reconstruction_prior'])


if __name__ == '__main__':
    unittest.main()
