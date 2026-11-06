"""Independent optical/head perturbations, withheld exposures and observability."""
import unittest

import numpy as np
from scipy.spatial.transform import Rotation

from test_room_measurement import observation, project
from room_reconstruction.measurement import observation_cameras
from room_reconstruction.stereo_rig_audit import joint_hypothesis, corrected_cameras, prediction_comparison


def fixture(stationary=False):
    cameras = []
    for i in range(12):
        phase = 0 if stationary else i*.65
        value = observation(.25*np.sin(phase), yaw=.12*np.cos(phase), moving=not stationary,
                            epoch_ns=1000000000+i*100000000)
        rotation = Rotation.from_euler('xz', (.08*np.sin(phase), .07*np.cos(phase))).as_matrix()
        translation = np.array((0, .08*np.cos(phase), .16*np.sin(phase*1.7)))
        for item in value['poses']:
            matrix = np.array(item['head']).reshape(3, 4)
            matrix[:, :3] = matrix[:, :3] @ rotation; matrix[:, 3] += translation
            item['head'] = matrix.ravel().tolist()
        matrix = np.array(value['reference_head']).reshape(3, 4)
        matrix[:, :3] = matrix[:, :3] @ rotation; matrix[:, 3] += translation
        value['reference_head'] = matrix.ravel().tolist()
        cameras.extend(observation_cameras(value, i))
    parameters = np.r_[np.deg2rad((.6, -.7, .4)), (.004, -.003, .008), np.deg2rad((.2, -.15, .1))]
    actual = corrected_cameras(cameras, parameters)
    points = [(x, y, z) for z in (-2., -3.) for y in (.9, 1.4, 2.) for x in (-.6, -.2, .2, .6)]
    pixels = [np.array([project(camera, p) for p in points]) for camera in actual]
    pairs = []
    for a in range(len(cameras)):
        for b in range(a+1, len(cameras)):
            if (cameras[a].frame == cameras[b].frame
                    or cameras[b].frame-cameras[a].frame == 1 and cameras[a].eye == cameras[b].eye):
                pairs.append({'a': a, 'b': b, 'pixels_a': pixels[a], 'pixels_b': pixels[b]})
    tracks = [[(i, pixels[i][j]) for i in range(len(cameras))] for j in range(len(points))]
    return cameras, parameters, pairs, tracks


class JointStereoRigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cameras, cls.parameters, cls.pairs, cls.tracks = fixture()
        cls.original = [camera.optical.copy() for camera in cls.cameras]
        cls.hypothesis = joint_hypothesis(cls.cameras, cls.pairs, descriptor_outlier_filter=False)

    def test_recovers_separate_head_and_optical_errors_without_changing_metric_baseline(self):
        self.assertEqual(self.hypothesis['status'], 'diagnostic_hypothesis_only')
        for fold in self.hypothesis['folds']:
            self.assertTrue(set(fold['held_out_frames']).isdisjoint(fold['training_frames']))
            self.assertTrue(fold['full_rank'])
            self.assertFalse(fold['at_parameter_bound'])
            self.assertLess(np.max(abs(np.array(fold['parameters'])-self.parameters)), 1e-7)
        corrected = corrected_cameras(self.cameras, self.parameters)
        for i in range(0, len(corrected), 2):
            before = np.linalg.norm(self.original[i][:, 3]-self.original[i+1][:, 3])
            after = np.linalg.norm(corrected[i].optical[:, 3]-corrected[i+1].optical[:, 3])
            self.assertAlmostEqual(before, after, places=12)
        for camera, original in zip(self.cameras, self.original):
            self.assertTrue(np.array_equal(camera.optical, original))
        self.assertFalse(self.hypothesis['reference_height_used'])
        self.assertFalse(self.hypothesis['accepted_for_live_installation'])

    def test_both_eyes_of_unseen_exposures_stay_outside_calibration_and_point_fits(self):
        result = prediction_comparison(self.cameras, self.tracks, self.hypothesis)
        self.assertLess(result['paired_same_tracks']['rig_hypothesis']['p95'], 1e-6)
        self.assertGreater(result['paired_same_tracks']['original']['median'], .3)
        self.assertTrue(result['calibration_and_point_fit_exclude_both_eyes_of_prediction_exposure'])
        # Corrupt X so the measured exposure row stays identical.
        corrupted = [[(i, pixel+np.array((7., 0.)) if self.cameras[i].frame == 6 else pixel)
                      for i, pixel in track] for track in self.tracks]
        scored = prediction_comparison(self.cameras, corrupted, self.hypothesis)
        self.assertEqual(scored['paired_same_tracks']['rig_hypothesis']['count'], 48)
        self.assertAlmostEqual(scored['paired_same_tracks']['rig_hypothesis']['p95'], 7., places=5)

    def test_repeated_stationary_views_cannot_certify_head_calibration(self):
        cameras, _, pairs, _ = fixture(stationary=True)
        result = joint_hypothesis(cameras, pairs, descriptor_outlier_filter=False)
        self.assertEqual(result['status'], 'unobservable_or_unbounded_hypothesis')
        self.assertFalse(all(fold['full_rank'] for fold in result['folds']))

    def test_out_of_budget_hypothesis_cannot_modify_camera_copies(self):
        bad = self.parameters.copy(); bad[3] = .04
        self.assertRaises(ValueError, corrected_cameras, self.cameras, bad)
        self.assertRaises(ValueError, corrected_cameras, self.cameras, np.zeros(6))
        for camera, original in zip(self.cameras, self.original):
            self.assertTrue(np.array_equal(camera.optical, original))


if __name__ == '__main__':
    unittest.main()
