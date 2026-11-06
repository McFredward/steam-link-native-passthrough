"""Independent metric intersections and optimization guards for the new core."""
import copy
import unittest

import numpy as np
import torch

from room_reconstruction.surface_gaussians import SurfaceGaussians
from room_reconstruction.surface_optimization import RayTrainingView, SurfaceOptimizer
from room_reconstruction.surface_mesh import measured_ray_mesh
from room_reconstruction.surface_candidates import ray_candidates


class SurfaceGaussianTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_two_metric_surfaces_composite_in_ray_depth_order(self):
        model = SurfaceGaussians(torch.tensor(((0., 0., -1.), (0., 0., -2.)), dtype=torch.float64),
                                 [[0, 0, 1]]*2, [[.2, .2]]*2, [[1, 0, 0], [0, 0, 1]], .5)
        ray = torch.tensor([[0., 0., -1.]], dtype=torch.float64)
        result = model.render(torch.zeros_like(ray), ray, torch.tensor([[1, 0]]))
        self.assertAlmostEqual(result['alpha'].item(), .75)
        self.assertAlmostEqual(result['depth'].item(), 4/3)
        self.assertAlmostEqual(result['median_depth'].item(), 1.)
        self.assertAlmostEqual(result['distortion'].item(), .25)
        self.assertAlmostEqual(result['normal_variance'].item(), 0.)
        self.assertTrue(torch.allclose(result['normal'], torch.tensor([[0., 0., 1.]], dtype=torch.float64)))
        expected = torch.tensor([[.5, 0., .25]], dtype=torch.float64)
        self.assertTrue(torch.allclose(result['color'], expected, atol=1e-5, rtol=0))

    def test_oblique_disk_intersections_share_a_fixed_world_surface_for_different_origins(self):
        # Known plane z = -2 + 0.25*x, evaluated at x=0.4 and x=-0.2.
        model = SurfaceGaussians(torch.tensor([[0., 0., -2.]], dtype=torch.float64),
                                 [[-.25, 0, 1]], [[1., 1.]], [[.6]], .9)
        origins = torch.tensor([[.1, -.1, .2], [-.3, .2, -.1]], dtype=torch.float64)
        expected = torch.tensor([[.4, .05, -1.9], [-.2, -.1, -2.05]], dtype=torch.float64)
        direction = expected-origins
        result = model.render(origins, direction, torch.zeros((2, 1), dtype=torch.int64))
        position = origins+torch.nn.functional.normalize(direction, dim=-1)*result['depth'][:, None]
        self.assertTrue(torch.allclose(position, expected, atol=1e-12, rtol=0))
        self.assertTrue(torch.allclose(result['median_depth'], torch.linalg.vector_norm(direction, dim=-1), atol=1e-12))

    def test_no_candidate_or_no_intersection_never_produces_a_surface(self):
        model = SurfaceGaussians([[0, 0, -2]], [[0, 0, 1]], [[.2, .2]], [[.5]], .3)
        origin = torch.zeros((4, 3))
        rays = torch.tensor([[0., 0., -1.], [0., 0., 1.], [1., 0., 0.], [0., 0., -1.]])
        result = model.render(origin, rays, torch.tensor([[-1], [0], [0], [0]]))
        self.assertTrue(torch.equal(result['alpha'][:3], torch.zeros(3)))
        self.assertTrue(torch.equal(result['depth'][:3], torch.zeros(3)))
        self.assertTrue(torch.equal(result['median_depth'], torch.zeros(4)))
        self.assertAlmostEqual(result['depth'][3].item(), 2.)
        self.assertGreater(result['alpha'][3].item(), 0.)

    def test_render_gradients_match_independent_finite_differences(self):
        model = SurfaceGaussians(torch.tensor([[.02, .03, -2.], [-.06, .02, -2.2]], dtype=torch.float64),
                                 [[.1, -.1, 1.], [-.05, .1, 1.]],
                                 [[.3, .25]]*2, [[.7, .3, .4], [.2, .8, .1]], .6)
        origin = torch.tensor([[.1, 0., 0.], [-.1, .05, 0.]], dtype=torch.float64)
        direction = torch.tensor([[0., 0., -1.], [.04, -.01, -1.]], dtype=torch.float64)
        candidates = torch.tensor([[0, 1], [0, 1]])
        def objective():
            result = model.render(origin, direction, candidates)
            return result['color'].square().sum()+.3*result['depth'].sum()+.1*result['distortion'].sum()
        objective().backward()
        for parameter, index in ((model.means, (0, 2)), (model.axes, (1, 0, 2)),
                                 (model.log_scales, (0, 1)), (model.opacity_logits, (1,)),
                                 (model.color_logits, (1, 2))):
            analytic = parameter.grad[index].item()
            with torch.no_grad():
                original = parameter[index].item(); epsilon = 1e-6
                parameter[index] = original+epsilon; plus = objective().item()
                parameter[index] = original-epsilon; minus = objective().item()
                parameter[index] = original
            self.assertAlmostEqual(analytic, (plus-minus)/(2*epsilon), places=6)

    def test_invisible_disks_and_momentum_stay_unchanged_while_other_regions_train(self):
        model = SurfaceGaussians([[0., 0., -2.], [1., 0., -2.]], [[0, 0, 1]]*2,
                                 [[.2, .2]]*2, [[.7], [.7]], .8)
        optimizer = SurfaceOptimizer(model)
        def view(index):
            return RayTrainingView(torch.tensor([[float(index), 0., 0.]]), torch.tensor([[.03, .01, -1.]]),
                torch.tensor([[.2]]), torch.tensor([[index]]), torch.tensor([True]), torch.tensor([False]))
        for _ in range(105):
            optimizer.step(view(0))
        frozen = {name: value[0].detach().clone() for name, value in model.named_parameters()}
        first_momentum = {name: (a[0].clone(), b[0].clone()) for name, (a, b) in optimizer.moments.items()}
        count = optimizer.counts[0].clone()
        for _ in range(20):
            optimizer.step(view(1))
        for name, value in model.named_parameters():
            self.assertTrue(torch.equal(value[0], frozen[name]), name)
            self.assertTrue(torch.equal(optimizer.moments[name][0][0], first_momentum[name][0]), name)
            self.assertTrue(torch.equal(optimizer.moments[name][1][0], first_momentum[name][1]), name)
        self.assertEqual(optimizer.counts[0], count)
        optimizer.step(view(0))
        self.assertEqual(optimizer.counts[0], count+1)

    def test_unknown_and_overflow_views_cannot_update_geometry_or_optimizer_state(self):
        model = SurfaceGaussians([[0., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        optimizer = SurfaceOptimizer(model)
        original = copy.deepcopy(model.state_dict())
        for candidate, valid, overflow in ((-1, True, False), (0, False, False), (0, True, True)):
            result = optimizer.step(RayTrainingView(torch.zeros((1, 3)), torch.tensor([[0., 0., -1.]]),
                torch.tensor([[.2]]), torch.tensor([[candidate]]), torch.tensor([valid]), torch.tensor([overflow])))
            self.assertFalse(result['updated'])
        for name, value in model.state_dict().items():
            self.assertTrue(torch.equal(value, original[name]))
        self.assertEqual(optimizer.counts.sum().item(), 0)

    def test_ray_work_and_parameter_inputs_have_explicit_bounds(self):
        self.assertRaises(ValueError, SurfaceGaussians, [[0, 0, -2]], [[0, 0, 0]], [[.2, .2]], [[.5]])
        model = SurfaceGaussians([[0., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        self.assertRaisesRegex(ValueError, 'budget', model.render, torch.zeros((1, 3)),
                               torch.tensor([[0., 0., -1.]]), torch.zeros((1, 129), dtype=torch.int64))
        self.assertRaisesRegex(ValueError, 'index', model.render, torch.zeros((1, 3)),
                               torch.tensor([[0., 0., -1.]]), torch.tensor([[1]]))

    def test_measured_mesh_keeps_unknown_holes_and_original_metric_positions(self):
        y, x = np.mgrid[-.1:.1:5j, -.1:.1:5j]
        target = np.column_stack((x.ravel(), y.ravel(), np.full(x.size, -2.)))
        origin = np.zeros_like(target)
        direction = target/np.linalg.norm(target, axis=-1)[:, None]
        normals = np.tile((0., 0., 1.), (len(target), 1))
        evidence = np.ones(len(target), bool); evidence[12] = False
        vertices, faces = measured_ray_mesh(origin, direction, np.linalg.norm(target, axis=-1), normals,
                                            evidence, width=5, height=5)
        self.assertEqual(len(vertices), 24)
        self.assertEqual(len(faces), 26)
        self.assertTrue(np.allclose(vertices[:, 2], -2.))
        self.assertFalse(np.any(np.linalg.norm(vertices[:, :2], axis=1) < 1e-12))
        cross = np.cross(vertices[faces[:, 1]]-vertices[faces[:, 0]], vertices[faces[:, 2]]-vertices[faces[:, 0]])
        self.assertTrue(np.all(cross[:, 2] > 0))

    def test_measured_mesh_does_not_bridge_a_nearby_depth_discontinuity(self):
        y, x = np.mgrid[-.1:.1:5j, -.1:.1:5j]
        z = np.where(x < 0, -2., -2.1)
        target = np.column_stack((x.ravel(), y.ravel(), z.ravel()))
        vertices, faces = measured_ray_mesh(np.zeros_like(target), target/np.linalg.norm(target, axis=-1)[:, None],
            np.linalg.norm(target, axis=-1), np.tile((0., 0., 1.), (len(target), 1)),
            np.ones(len(target), bool), width=5, height=5)
        self.assertEqual(len(faces), 24)
        self.assertTrue(np.all(np.ptp(vertices[faces, 2], axis=1) < 1e-10))

    def test_world_ray_candidates_are_complete_across_chunks_and_report_capacity(self):
        model = SurfaceGaussians([[0., 0., -1.], [0., 0., -2.], [2., 0., -2.]],
                                 [[0, 0, 1]]*3, [[.05, .05]]*3, [[.5]]*3)
        origin = torch.tensor([[0., 0., 0.], [2., 0., 0.]])
        directions = torch.tensor([[0., 0., -1.]]*2)
        result = ray_candidates(model, origin, directions, capacity=3, ray_chunk=1, point_chunk=1)
        self.assertEqual(set(result.indices[0].tolist()), {-1, 0, 1})
        self.assertEqual(set(result.indices[1].tolist()), {-1, 2})
        self.assertTrue(torch.equal(result.counts, torch.tensor([2, 1])))
        small = ray_candidates(model, origin, directions, capacity=1, point_chunk=2)
        self.assertTrue(torch.equal(small.overflow, torch.tensor([True, False])))

    def test_candidate_cache_invalidates_before_geometry_can_leave_its_margin(self):
        model = SurfaceGaussians([[0., 0., -2.]], [[0, 0, 1]], [[.05, .05]], [[.5]])
        origin = torch.zeros((1, 3)); directions = torch.tensor([[0., 0., -1.]])
        result = ray_candidates(model, origin, directions, motion_margin_m=.04)
        self.assertTrue(result.covers(model, origin, directions))
        with torch.no_grad():
            model.means[0, 0] += .05
        self.assertFalse(result.covers(model, origin, directions))
        with torch.no_grad():
            model.means[0, 0] -= .05
        self.assertFalse(result.covers(model, origin+.05, directions))
        self.assertFalse(result.covers(model, origin, torch.tensor([[.02, 0., -1.]])))
        with torch.no_grad():
            model.log_scales += .4
        self.assertFalse(result.covers(model, origin, directions))

    def test_noisy_ceiling_seed_is_corrected_using_training_images_only(self):
        from room_reconstruction.surface_control import run_planar_control
        report, _ = run_planar_control('ceiling', steps=3000, normal_noise_radians=.03, optimize_normals=True)
        self.assertTrue(report['synthetic_control_passed'], report)
        self.assertGreater(report['held_out_before_depth_median_error_m'], .1)
        self.assertLess(report['held_out_depth_p95_error_m'], .05)
        self.assertEqual(report['held_out_supported_ray_fraction'], 1.)
        self.assertEqual(report['held_out_candidate_overflow_rays'], 0)
        self.assertTrue(report['initial_seed_rays_and_colors_from_training_images_only'])
        self.assertFalse(report['reference_geometry_used_as_optimization_prior'])
        self.assertFalse(report['held_out_image_used_in_optimization'])
        self.assertFalse(report['ground_truth_alignment_fitted'])
        self.assertFalse(report['accepted_for_live_installation'])


if __name__ == '__main__':
    unittest.main()
