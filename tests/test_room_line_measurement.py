"""Independent analytic lines, real row cameras and exposure-disjoint scoring."""
import unittest
import numpy as np

from test_room_measurement import observation, project
from room_reconstruction.measurement import observation_cameras
from room_reconstruction.line_measurement import (
    MeasuredLine, fit_measured_line, line_prediction_errors,
)


def fixture(vertical=.15, *, different_segments=False, horizontal=False):
    cameras = []
    for frame, position in enumerate((-1., -.3, .4, 1.)):
        value = observation(position*.08, yaw=position*.04, moving=True,
                            epoch_ns=1000000000+frame*100000000)
        for item in value['poses']:
            transform = np.array(item['head']).reshape(3, 4)
            transform[1, 3] += vertical*position
            item['head'] = transform.ravel().tolist()
        # Retain the independently sampled historical row transforms.
        reference = np.array(value['reference_head']).reshape(3, 4)
        reference[1, 3] += vertical*position
        value['reference_head'] = reference.ravel().tolist()
        cameras.extend(observation_cameras(value, frame))
    point = np.array((.15, 2.1, -2.4))
    direction = np.array((1., 0., 0.) if horizontal else (.8, .1, -.2))
    direction /= np.linalg.norm(direction)
    measurements = []
    for index, camera in enumerate(cameras):
        # Endpoints may cover different sections of the same physical edge.
        offset = (index//2-1.5)*.4 if different_segments else 0
        parameters = np.linspace(-.25, .25, 9)+offset
        pixels = np.array([project(camera, point+t*direction) for t in parameters])
        measurements.append((index, pixels))
    return cameras, point, direction, measurements


class LineMeasurementTests(unittest.TestCase):
    def test_recovers_metric_line_with_moving_exposure_rows(self):
        cameras, point, direction, measurements = fixture()
        original = [camera.optical.copy() for camera in cameras]
        line = fit_measured_line(cameras, measurements)
        self.assertIsNotNone(line)
        self.assertLess(np.linalg.norm(np.cross(line.direction, direction)), 1e-7)
        self.assertLess(np.linalg.norm(np.cross(point-line.point, direction)), 1e-7)
        self.assertLess(np.max(abs(line.residual_pixels)), 1e-6)
        self.assertLess(line.transverse_sigma_m, .03)
        self.assertEqual(len(line.observation_intervals), len(measurements))
        for camera, expected in zip(cameras, original):
            self.assertTrue(np.array_equal(camera.optical, expected))

    def test_different_observed_segments_do_not_require_endpoint_matches(self):
        cameras, point, direction, measurements = fixture(different_segments=True)
        line = fit_measured_line(cameras, measurements)
        self.assertIsNotNone(line)
        self.assertLess(np.linalg.norm(np.cross(point-line.point, direction)), 1e-7)
        intervals = line.observation_intervals
        self.assertEqual([i['camera'] for i in intervals], list(range(len(cameras))))
        # Keep each observation's measured interval instead of claiming the
        # whole infinite line, merging intervals or generating a surface hull.
        self.assertGreater(abs(intervals[0]['parameter_min_m']-intervals[-1]['parameter_min_m']), 1.)

    def test_both_eyes_of_prediction_exposure_stay_out_of_fit_and_bad_predictions_remain(self):
        cameras, _, _, measurements = fixture()
        training = [m for m in measurements if cameras[m[0]].frame != 1]
        held = [m for m in measurements if cameras[m[0]].frame == 1]
        line = fit_measured_line(cameras, training)
        self.assertIsNotNone(line)
        self.assertLess(np.max(abs(line_prediction_errors(cameras, line, held))), 1e-6)
        bad = [(i, pixels+np.array((0., 7.))) for i, pixels in held]
        errors = line_prediction_errors(cameras, line, bad)
        self.assertEqual(len(errors), 18)
        self.assertGreater(np.quantile(abs(errors), .95), 6.)
        behind = MeasuredLine(np.array((0., 2.1, 2.4)), np.array((1., 0., 0.)),
                              np.zeros(0), [], 0., 1.)
        impossible = line_prediction_errors(cameras, behind, held)
        self.assertEqual(len(impossible), 18)
        self.assertTrue(np.isinf(impossible).all())

    def test_translation_along_horizontal_edge_cannot_establish_its_range(self):
        cameras, _, _, measurements = fixture(vertical=0, horizontal=True)
        self.assertIsNone(fit_measured_line(cameras, measurements))

    def test_short_transverse_baseline_increases_conditional_position_uncertainty(self):
        weak = fit_measured_line(*self._fit_arguments(.025))
        strong = fit_measured_line(*self._fit_arguments(.25))
        self.assertIsNotNone(weak)
        self.assertIsNotNone(strong)
        self.assertGreater(weak.transverse_sigma_m, strong.transverse_sigma_m*3)

    def test_more_samples_on_one_segment_do_not_manufacture_independent_evidence(self):
        cameras, point, direction, measurements = fixture()
        line = fit_measured_line(cameras, measurements)
        dense = [(i, np.array([project(camera, point+t*direction) for t in np.linspace(-.25, .25, 31)]))
                 for i, camera in enumerate(cameras)]
        sampled = fit_measured_line(cameras, dense)
        self.assertIsNotNone(sampled)
        self.assertGreater(sampled.transverse_sigma_m, line.transverse_sigma_m*.8)
        self.assertLess(sampled.transverse_sigma_m, line.transverse_sigma_m*1.2)

    @staticmethod
    def _fit_arguments(vertical):
        cameras, _, _, measurements = fixture(vertical=vertical, horizontal=True)
        return cameras, measurements

    def test_invalid_and_unbounded_inputs_are_refused_before_fitting(self):
        cameras, _, _, measurements = fixture()
        self.assertRaises(ValueError, fit_measured_line, cameras, measurements*129)
        self.assertRaises(ValueError, fit_measured_line, cameras, [measurements[0]]*2)
        self.assertRaises(ValueError, fit_measured_line, cameras, [(False, measurements[0][1])])
        self.assertRaises(ValueError, fit_measured_line, cameras, [(0, np.zeros((33, 2)))])
        self.assertRaises(ValueError, fit_measured_line, cameras, [(0, np.full((9, 2), np.nan))])
        self.assertIsNone(fit_measured_line(cameras, measurements[:2]))


if __name__ == '__main__':
    unittest.main()
