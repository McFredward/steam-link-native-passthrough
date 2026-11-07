"""Analytic controls for polar lens geometry; no private camera coefficients."""
import copy
import unittest
import numpy as np

from room_reconstruction.live_geometry import project_kb
from room_reconstruction.measurement import intersect_rays
from room_reconstruction.polar_lens import PolarSplineLens


def specification(radial=0., tangential=0.):
    return {'cameraModel': 'kb_polar_spline', 'gridSize': [5, 5],
            'fx': 1400., 'fy': 1420., 'cx': 1200., 'cy': 1210.,
            'k1': -.02, 'k2': .001, 'k3': 0., 'k4': 0.,
            'nodeSpacingR': 250., 'sR': [radial]*25, 'sT': [tangential]*25}


class PolarLensTests(unittest.TestCase):
    def test_zero_displacements_retain_the_complete_kb_projection(self):
        model = PolarSplineLens(specification())
        rays = np.array(((0., 0., 1.), (.2, -.3, 1.), (-1., .7, 1.)))
        expected = project_kb(rays, model.intrinsics, model.coefficients)
        np.testing.assert_array_equal(model.project(rays), expected)

    def test_radial_and_tangential_values_rotate_into_cartesian_pixels(self):
        model = PolarSplineLens(specification(3., 2.))
        # At radius three the entire interpolation neighbourhood is constant;
        # expected shifts follow the independently known polar coordinate axes.
        pixels = np.array(((1950., 1210.), (1200., 1960.), (450., 1210.), (1200., 460.)))
        expected = np.array(((3., 2.), (-2., 3.), (-3., -2.), (2., -3.)))
        np.testing.assert_allclose(model.pixel_shift(pixels), expected, atol=1e-12)
        np.testing.assert_allclose(model.pixel_shift([[1200., 1210.]]), [[0., 0.]], atol=1e-12)

    def test_angular_seam_is_continuous_and_radial_edge_stays_bounded(self):
        value = specification()
        value['sR'] = list(np.tile((1., 2., 4., 3., 0.), 5))
        value['sT'] = list(np.tile((2., 0., 1., -1., 3.), 5))
        model = PolarSplineLens(value)
        shift = model.pixel_shift(np.array(((1950., 1210.-1e-6), (1950., 1210.+1e-6))))
        self.assertLess(np.linalg.norm(shift[0]-shift[1]), 1e-6)
        distant = model.pixel_shift(np.array(((31200., 1210.), (41200., 1210.))))
        np.testing.assert_allclose(distant, np.tile((1., 2.), (2, 1)), atol=1e-12)

    def test_metric_stereo_recovers_independent_points_with_distorted_lenses(self):
        model = PolarSplineLens(specification(2.5, -.9))
        centres = np.array(((-.032, 0., 0.), (.032, 0., 0.)))
        for point in np.array(((-.4, .6, 1.5), (.5, -.4, 2.8), (.1, .2, 4.))):
            pixels = model.project(point-centres)
            rays, valid = model.unproject(pixels)
            self.assertTrue(valid.all())
            recovered = intersect_rays(centres, rays)
            self.assertIsNotNone(recovered)
            np.testing.assert_allclose(recovered, point, atol=1e-9)

    def test_folded_kb_branch_and_unconverged_spline_are_refused(self):
        value = specification()
        value['k1'] = -1.
        model = PolarSplineLens(value)
        _, valid = model.unproject([[2200., 1210.]])
        self.assertFalse(valid[0])
        wild = specification()
        wild['sR'] = [10000.]*25
        _, valid = PolarSplineLens(wild).unproject([[2200., 1210.]])
        self.assertFalse(valid[0])
        np.testing.assert_array_equal(model.project([[0., 0., -1.]]), [[-100., -100.]])

    def test_invalid_models_and_nonfinite_inputs_are_refused(self):
        for key, invalid in (('gridSize', [4, 5]), ('nodeSpacingR', 0), ('nodeSpacingR', float('nan')),
                             ('fx', -1.), ('sR', [0.]*24), ('sT', [float('nan')]*25)):
            value = copy.deepcopy(specification()); value[key] = invalid
            with self.subTest(key=key, invalid=str(invalid)[:20]), self.assertRaises(ValueError):
                PolarSplineLens(value)
        model = PolarSplineLens(specification())
        with self.assertRaises(ValueError):
            model.pixel_shift([[float('nan'), 0.]])
        with self.assertRaises(ValueError):
            model.project([[0., float('inf'), 1.]])
        with self.assertRaises(ValueError):
            model.unproject([[float('inf'), 0.]])


if __name__ == '__main__':
    unittest.main()
