"""Independent metric intersections and optimization guards for the new core."""
import copy
from dataclasses import replace
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

    def test_occluded_disks_keep_geometry_appearance_and_existing_momentum(self):
        model = SurfaceGaussians([[0., 0., -2.], [0., 0., -4.]], [[0, 0, 1]]*2,
                                 [[.1, .1]]*2, [[.7], [.5]], [.9999, .8])
        optimizer = SurfaceOptimizer(model, random_background=False)
        visible = RayTrainingView(torch.tensor([[2., 0., 0.]]), torch.tensor([[-1.97, 0., -4.]]),
            torch.tensor([[.2]]), torch.tensor([[0, 1]]), torch.tensor([True]), torch.tensor([False]))
        for _ in range(5):
            optimizer.step(visible)
        self.assertGreater(optimizer.moments['means'][0][1].abs().sum().item(), 0.)
        frozen = {name: value[1].detach().clone() for name, value in model.named_parameters()}
        momentum = {name: (a[1].clone(), b[1].clone()) for name, (a, b) in optimizer.moments.items()}
        count = optimizer.counts[1].clone()
        occluded = replace(visible, origins=torch.zeros((1, 3)), directions=torch.tensor([[0., 0., -1.]]),
                           colors=torch.tensor([[.7]]))
        for _ in range(30):
            optimizer.step(occluded)
        self.assertEqual(optimizer.counts[1], count)
        for name, value in model.named_parameters():
            self.assertTrue(torch.equal(value[1], frozen[name]), name)
            self.assertTrue(torch.equal(optimizer.moments[name][0][1], momentum[name][0]), name)
            self.assertTrue(torch.equal(optimizer.moments[name][1][1], momentum[name][1]), name)

    def test_occluded_ray_cannot_train_a_disk_visible_in_another_ray_in_the_same_batch(self):
        model = SurfaceGaussians([[0., 0., -2.], [0., 0., -4.]], [[0, 0, 1]]*2,
                                 [[.1, .1]]*2, [[.7], [.5]], [.9999, .8])
        baseline = copy.deepcopy(model)
        visible = RayTrainingView(torch.tensor([[2., 0., 0.]]), torch.tensor([[-1.97, 0., -4.]]),
            torch.tensor([[.2]]), torch.tensor([[0, 1]]), torch.tensor([True]), torch.tensor([False]))
        mixed = replace(visible, origins=torch.cat((torch.zeros((1, 3)), visible.origins)),
            directions=torch.cat((torch.tensor([[0., 0., -1.]]), visible.directions)),
            colors=torch.tensor([[.7], [.2]]), candidates=torch.tensor([[0, 1], [0, 1]]),
            valid=torch.tensor([True, True]), overflow=torch.tensor([False, False]))
        SurfaceOptimizer(model, random_background=False).step(mixed)
        SurfaceOptimizer(baseline, random_background=False).step(visible)
        for name, parameter in model.named_parameters():
            other = dict(baseline.named_parameters())[name]
            # The additional foreground ray changes the mean's denominator,
            # but contributes no background gradient in any parameter family.
            self.assertTrue(torch.allclose(parameter.grad[1]*2, other.grad[1], atol=2e-6, rtol=2e-5), name)
        self.assertGreater(model.means.grad[1].abs().sum().item(), 0.)

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

    def test_independent_metric_observation_corrects_photometrically_ambiguous_depth(self):
        # A constant-intensity ray alone cannot determine its surface range.
        # An independent training measurement constrains depth, not a room plane.
        model = SurfaceGaussians([[0., 0., -2.12]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        optimizer = SurfaceOptimizer(model)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[0., 0., -1.]]),
            torch.tensor([[.7]]), torch.tensor([[0]]), torch.tensor([True]), torch.tensor([False]),
            measured_depth=torch.tensor([2.]), measured_sigma=torch.tensor([.02]))
        for _ in range(250):
            result = optimizer.step(view)
        self.assertEqual(result['metric_constraint_rays'], 1)
        self.assertLess(abs(model.means[0, 2].item()+2), .003)

    def test_unknown_metric_depth_is_neutral_and_invalid_uncertainty_refuses_an_update(self):
        model = SurfaceGaussians([[0., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        other = copy.deepcopy(model)
        left, right = SurfaceOptimizer(model), SurfaceOptimizer(other)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[.03, .01, -1.]]),
            torch.tensor([[.2]]), torch.tensor([[0]]), torch.tensor([True]), torch.tensor([False]))
        left.step(view)
        result = right.step(replace(view, measured_depth=torch.tensor([0.]), measured_sigma=torch.tensor([0.])))
        self.assertEqual(result['metric_constraint_rays'], 0)
        for name, value in model.state_dict().items():
            self.assertTrue(torch.equal(value, other.state_dict()[name]))
        frozen = copy.deepcopy(model.state_dict()); count = left.counts.clone()
        for depth, sigma in (([2.], None), ([2.], [0.]), ([0.], [.02]), ([float('nan')], [.02]),
                             ([6.1], [.02]), ([2.], [.001]), ([2., 2.], [.02, .02])):
            with self.assertRaisesRegex(ValueError, 'depth constraints|measured uncertainty'):
                left.step(replace(view, measured_depth=torch.tensor(depth),
                                  measured_sigma=None if sigma is None else torch.tensor(sigma)))
            self.assertTrue(torch.equal(left.counts, count))
            for name, value in model.state_dict().items():
                self.assertTrue(torch.equal(value, frozen[name]))

    def test_opposite_surface_errors_cannot_cancel_in_a_metric_mean(self):
        # Ray weights are .5 and .25. Their expected depth is exactly 2 m,
        # although neither terminating surface is at the measured 2 m.
        model = SurfaceGaussians([[0., 0., -1.8], [0., 0., -2.4]], [[0, 0, 1]]*2,
                                 [[.2, .2]]*2, [[.7]]*2, .5)
        baseline = copy.deepcopy(model)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[0., 0., -1.]]),
            torch.tensor([[.7]]), torch.tensor([[0, 1]]), torch.tensor([True]), torch.tensor([False]))
        before = model.render(view.origins, view.directions, view.candidates)
        self.assertAlmostEqual(before['depth'].item(), 2., places=5)
        self.assertGreater(abs(before['median_depth'].item()-2.), .19)
        measured = SurfaceOptimizer(model, metric_weight=.1).step(replace(view,
            measured_depth=torch.tensor([2.]), measured_sigma=torch.tensor([.02])))
        unmeasured = SurfaceOptimizer(baseline, metric_weight=.1).step(view)
        # The foreground costs ten sigma; the censored background costs three.
        self.assertAlmostEqual(measured['loss']-unmeasured['loss'], .1*(.5*10+.25*3)/.75, places=4)
        self.assertGreater((model.means.grad-baseline.means.grad)[0, 2].item(), 0.)

    def test_a_foreground_metric_ray_does_not_pull_occluded_background_geometry(self):
        model = SurfaceGaussians([[0., 0., -1.98], [0., 0., -4.]], [[0, 0, 1]]*2,
                                 [[.2, .2]]*2, [[.7]]*2, .5)
        baseline = copy.deepcopy(model)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[0., 0., -1.]]),
            torch.tensor([[.7]]), torch.tensor([[0, 1]]), torch.tensor([True]), torch.tensor([False]))
        SurfaceOptimizer(model, metric_weight=.1).step(replace(view,
            measured_depth=torch.tensor([2.]), measured_sigma=torch.tensor([.02])))
        SurfaceOptimizer(baseline, metric_weight=.1).step(view)
        gradient = model.means.grad-baseline.means.grad
        self.assertGreater(gradient[0, 2].item(), 0.)
        self.assertTrue(torch.allclose(gradient[1], torch.zeros(3), atol=1e-7, rtol=0))
        # Metric error cannot be reduced by changing the background's opacity,
        # footprint, normal or color instead of correcting the foreground.
        for name in ('axes', 'log_scales', 'opacity_logits', 'color_logits'):
            a, b = dict(model.named_parameters())[name], dict(baseline.named_parameters())[name]
            self.assertTrue(torch.allclose(a.grad[1], b.grad[1], atol=1e-7, rtol=0), name)

    def test_a_missing_foreground_does_not_repurpose_a_remote_background(self):
        model = SurfaceGaussians([[0., 0., -4.]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        baseline = copy.deepcopy(model)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[0., 0., -1.]]),
            torch.tensor([[.7]]), torch.tensor([[0]]), torch.tensor([True]), torch.tensor([False]))
        measured = SurfaceOptimizer(model, metric_weight=.1).step(replace(view,
            measured_depth=torch.tensor([2.]), measured_sigma=torch.tensor([.02])))
        unmeasured = SurfaceOptimizer(baseline, metric_weight=.1).step(view)
        self.assertGreater(measured['loss']-unmeasured['loss'], .29)
        for name, parameter in model.named_parameters():
            other = dict(baseline.named_parameters())[name]
            self.assertTrue(torch.equal(parameter.grad, other.grad), name)
            self.assertTrue(torch.equal(parameter, other), name)
        # A new measured surface needs caller admission; the absent foreground
        # cannot be manufactured by dragging this background toward the camera.

    def test_metric_depth_overcomes_a_wrong_neural_position_prior(self):
        model = SurfaceGaussians([[0., 0., -2.12]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        optimizer = SurfaceOptimizer(model, seed_directions=[[0., 0., -1.]],
                                     seed_position_sigma_m=.10, metric_weight=.1)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[0., 0., -1.]]),
            torch.tensor([[.7]]), torch.tensor([[0]]), torch.tensor([True]), torch.tensor([False]))
        original_sigma = optimizer.seed_position_sigma_m.clone()
        for _ in range(10):
            result = optimizer.step(view)
        self.assertEqual(result['metric_constraint_rays'], 0)
        self.assertAlmostEqual(model.means[0, 2].item(), -2.12, places=6)
        measured = replace(view, measured_depth=torch.tensor([2.]), measured_sigma=torch.tensor([.02]))
        for _ in range(250):
            result = optimizer.step(measured)
        self.assertEqual(result['metric_constraint_rays'], 1)
        self.assertLess(abs(model.means[0, 2].item()+2), .004)
        self.assertTrue(torch.equal(optimizer.seed_position_sigma_m, original_sigma))

    def test_extension_preserves_old_parameters_momentum_clocks_and_candidate_ids(self):
        model = SurfaceGaussians([[0., 0., -2.12]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        optimizer = SurfaceOptimizer(model, seed_directions=[[0., 0., -1.]],
                                     seed_position_sigma_m=.10, metric_weight=.1)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[0., 0., -1.]]),
            torch.tensor([[.7]]), torch.tensor([[0]]), torch.tensor([True]), torch.tensor([False]),
            measured_depth=torch.tensor([2.]), measured_sigma=torch.tensor([.02]))
        for _ in range(105):
            optimizer.step(view)
        original = copy.deepcopy(model.state_dict())
        moments = copy.deepcopy(optimizer.moments)
        count, steps = optimizer.counts.clone(), optimizer.steps
        rng = optimizer.generator.get_state().clone()
        seeds = optimizer.seed_means.clone()
        cache = ray_candidates(model, view.origins, view.directions, capacity=8)
        addition = SurfaceGaussians([[1., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.6]], .8)
        grown = optimizer.extend(addition, seed_directions=[[0., 0., -1.]], seed_position_sigma_m=.08)
        self.assertIs(grown, optimizer.model)
        self.assertEqual(len(grown.means), 2)
        self.assertFalse(cache.covers(grown, view.origins, view.directions))
        self.assertEqual(optimizer.steps, steps)
        self.assertTrue(torch.equal(optimizer.generator.get_state(), rng))
        self.assertTrue(torch.equal(optimizer.seed_means[:1], seeds))
        self.assertTrue(torch.equal(optimizer.counts, torch.cat((count, torch.tensor([0])))))
        for name, value in grown.state_dict().items():
            self.assertTrue(torch.equal(value[:1], original[name]), name)
            self.assertTrue(torch.equal(model.state_dict()[name], original[name]), name)
            for actual, expected in zip(optimizer.moments[name], moments[name]):
                self.assertTrue(torch.equal(actual[:1], expected), name)
                self.assertEqual(torch.count_nonzero(actual[1:]).item(), 0)
        other = replace(view, origins=torch.tensor([[1., 0., 0.]]), candidates=torch.tensor([[1]]),
                        directions=torch.tensor([[.03, .01, -1.]]))
        for _ in range(20):
            optimizer.step(other)
        # A nonzero prior displacement on the old region must not pull it back
        # while the newly inserted region trains.
        for name, value in grown.state_dict().items():
            self.assertTrue(torch.equal(value[:1], original[name]), name)
            for actual, expected in zip(optimizer.moments[name], moments[name]):
                self.assertTrue(torch.equal(actual[:1], expected), name)
        self.assertEqual(optimizer.counts[0], count[0])
        self.assertEqual(optimizer.counts[1].item(), 20)
        optimizer.step(view)
        self.assertEqual(optimizer.counts[0], count[0]+1)

    def test_rejected_extension_leaves_the_entire_active_model_untouched(self):
        model = SurfaceGaussians([[0., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        optimizer = SurfaceOptimizer(model, seed_directions=[[0., 0., -1.]], seed_position_sigma_m=.1)
        original = copy.deepcopy(model.state_dict())
        addition = SurfaceGaussians([[1., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.5]], .8)
        bad_channels = SurfaceGaussians([[1., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.5, .5, .5]], .8)
        for value, direction, sigma in ((bad_channels, [[0., 0., -1.]], .1),
                (addition, None, .1), (addition, [[0., 0., 0.]], .1),
                (addition, [[0., 0., -1.]], float('nan')), (addition, [[0., 0., -1.]], [.1, .1])):
            with self.assertRaises(ValueError):
                optimizer.extend(value, seed_directions=direction, seed_position_sigma_m=sigma)
            self.assertIs(optimizer.model, model)
            self.assertEqual(optimizer.counts.sum().item(), 0)
            self.assertEqual(optimizer.steps, 0)
            for name, parameter in model.state_dict().items():
                self.assertTrue(torch.equal(parameter, original[name]), name)
                self.assertTrue(all(torch.count_nonzero(v).item() == 0 for v in optimizer.moments[name]))

    def test_position_priors_need_source_rays_and_keep_unknown_priors_neutral(self):
        model = SurfaceGaussians([[0., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        with self.assertRaisesRegex(ValueError, 'source-ray'):
            SurfaceOptimizer(model, seed_position_sigma_m=.1)
        for sigma in (-.1, .001, .6, float('inf')):
            with self.subTest(sigma=sigma), self.assertRaises(ValueError):
                SurfaceOptimizer(model, seed_directions=[[0., 0., -1.]], seed_position_sigma_m=sigma)
        other = copy.deepcopy(model)
        a = SurfaceOptimizer(model, seed_directions=[[0., 0., -1.]])
        b = SurfaceOptimizer(other, seed_directions=[[0., 0., -1.]], seed_position_sigma_m=0.)
        view = RayTrainingView(torch.zeros((1, 3)), torch.tensor([[.03, .01, -1.]]),
            torch.tensor([[.2]]), torch.tensor([[0]]), torch.tensor([True]), torch.tensor([False]))
        a.step(view); b.step(view)
        for name, value in model.state_dict().items():
            self.assertTrue(torch.equal(value, other.state_dict()[name]), name)

    def test_ray_work_and_parameter_inputs_have_explicit_bounds(self):
        self.assertRaises(ValueError, SurfaceGaussians, [[0, 0, -2]], [[0, 0, 0]], [[.2, .2]], [[.5]])
        model = SurfaceGaussians([[0., 0., -2.]], [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
        self.assertRaisesRegex(ValueError, 'budget', model.render, torch.zeros((1, 3)),
                               torch.tensor([[0., 0., -1.]]), torch.zeros((1, 513), dtype=torch.int64))
        self.assertRaisesRegex(ValueError, 'budget', model.render, torch.zeros((1025, 3)),
                               torch.tensor([[0., 0., -1.]]).repeat(1025, 1), torch.zeros((1025, 512), dtype=torch.int64))
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

    def test_smaller_ray_batches_can_cover_dense_regions_without_truncating_them(self):
        points = torch.zeros((200, 3)); points[:, 2] = -torch.linspace(1., 3., 200)
        model = SurfaceGaussians(points, torch.tensor([[0., 0., 1.]]).repeat(200, 1),
                                 torch.full((200, 2), .02), torch.full((200, 1), .5), .01)
        origins = torch.zeros((2, 3)); directions = torch.tensor([[0., 0., -1.]]*2)
        small = ray_candidates(model, origins, directions)
        self.assertTrue(small.overflow.all())
        complete = ray_candidates(model, origins, directions, capacity=256, point_chunk=37)
        self.assertFalse(complete.overflow.any())
        self.assertTrue(torch.equal(complete.counts, torch.tensor([200, 200])))
        self.assertEqual(set(complete.indices[0].tolist()), set(range(200)) | {-1})
        rendered = model.render(origins, directions, complete.indices)
        self.assertAlmostEqual(rendered['alpha'][0].item(), 1-.99**200, places=5)
        self.assertRaisesRegex(ValueError, 'bounded', ray_candidates, model, torch.zeros((1025, 3)),
                               directions[:1].repeat(1025, 1), capacity=512)

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
