"""Independent analytic image controls for metric surface-Gaussian trials.

The optimizer receives only biased seeds, camera rays and images. Reference
planes and held-out images are evaluated afterwards, never supplied as a loss.
This fixture is synthetic developer evidence, never a captured headset room.
"""
from __future__ import annotations

import argparse
from dataclasses import replace
import json
from pathlib import Path
import time

import numpy as np
from scipy.spatial.transform import Rotation
import torch

from .surface_gaussians import SurfaceGaussians
from .surface_candidates import ray_candidates
from .surface_optimization import RayTrainingView, SurfaceOptimizer


def texture(points):
    # Analytic image formation on the plane, independent of Gaussian rendering.
    return np.column_stack((.5+.25*np.sin(points[:, 0]*9)+.12*np.cos(points[:, 1]*7),
                            .5+.25*np.sin(points[:, 1]*8+.7)+.1*np.cos(points[:, 0]*5),
                            .5+.2*np.cos(points[:, 0]*8+points[:, 1]*7)))


def planar_fixture(orientation='front', *, normal_noise_radians=0., device='cpu'):
    if orientation not in ('front', 'floor', 'ceiling') or not 0 <= normal_noise_radians <= .1:
        raise ValueError('Invalid synthetic planar control')
    rotation = Rotation.from_euler('x', {'front': 0, 'floor': -90, 'ceiling': 90}[orientation], degrees=True).as_matrix()
    translation = np.array((.7, {'front': 1.3, 'floor': 2., 'ceiling': .7}[orientation], -.4))
    rng = np.random.default_rng(76)
    tensor = lambda array: torch.tensor(array, dtype=torch.float32, device=device)
    sy, sx = np.mgrid[0:23:19j, 0:23:19j]
    seed_ray = np.stack(((sx-11.5)/35, -(sy-11.5)/35, -np.ones_like(sx)), -1).reshape(-1, 3)
    seed_ray /= np.linalg.norm(seed_ray, axis=1)[:, None]
    means, seed_colors, seed_directions = [], [], []
    for offset in (-.32, -.16, .16, .32):
        origin = np.array((offset, 0., 0.))
        proposed_range = -2.12/seed_ray[:, 2]+rng.normal(0, .008, len(seed_ray))
        means.append(origin+proposed_range[:, None]*seed_ray)
        seed_directions.append(seed_ray)
        seed_colors.append(texture(origin+(-2/seed_ray[:, 2:])*seed_ray))
    canonical = np.concatenate(means)
    seed_colors = np.concatenate(seed_colors)
    seed_directions = np.concatenate(seed_directions)
    # Preserve each selected seed's original training-camera ray and color.
    # Held-out pixels never initialize the model, including its appearance.
    _, selected = np.unique(np.floor(canonical/.08).astype(np.int32), axis=0, return_index=True)
    canonical, seed_colors, seed_directions = canonical[selected], seed_colors[selected], seed_directions[selected]
    seed_normals = np.tile((0., 0., 1.), (len(canonical), 1))
    seed_normals += rng.normal(0, normal_noise_radians, seed_normals.shape)
    seed_normals /= np.linalg.norm(seed_normals, axis=1)[:, None]
    means = canonical @ rotation.T+translation
    model = SurfaceGaussians(tensor(means), tensor(seed_normals @ rotation.T),
        tensor(np.full((len(means), 2), .08)), tensor(seed_colors), .2)
    y, x = np.mgrid[:24, :24]
    direction = np.stack(((x-11.5)/35, -(y-11.5)/35, -np.ones_like(x)), -1).reshape(-1, 3)
    direction /= np.linalg.norm(direction, axis=1)[:, None]
    grid = np.arange(24*24).reshape(24, 24)
    a = grid[:-1, :-1].ravel()
    neighbors = torch.tensor(np.stack((a, a+1, a+24), -1), dtype=torch.int64, device=device)
    views, reference_ranges = [], []
    for offset in (-.32, -.16, 0, .16, .32):
        origin = np.tile((offset, 0., 0.), (len(direction), 1))
        # The real plane is used only to synthesize the captured RGB image and
        # independent metric reference. Candidate lookup uses the biased seed.
        point = origin+(-2/direction[:, 2:])*direction
        views.append(RayTrainingView(tensor(origin @ rotation.T+translation), tensor(direction @ rotation.T),
            tensor(texture(point)), torch.full((len(direction), 128), -1, dtype=torch.int64, device=device),
            torch.ones(len(direction), dtype=torch.bool, device=device),
            torch.zeros(len(direction), dtype=torch.bool, device=device), neighbors))
        reference_ranges.append(-2/direction[:, 2])
    return model, views, {'ranges': reference_ranges, 'rotation': rotation, 'translation': translation,
                           'seed_directions': seed_directions @ rotation.T,
                           'true_plane_point': np.array((0., 0., -2.)) @ rotation.T+translation,
                           'true_plane_normal': np.array((0., 0., 1.)) @ rotation.T}


def run_planar_control(orientation='front', *, steps=3000, normal_noise_radians=0.,
                       optimize_normals=False, device='cpu'):
    if type(steps) is not int or not 1 <= steps <= 10000:
        raise ValueError('Invalid synthetic optimization step budget')
    model, views, reference = planar_fixture(orientation, normal_noise_radians=normal_noise_radians, device=device)
    optimizer = SurfaceOptimizer(model, optimize_normals=optimize_normals, seed_directions=reference['seed_directions'])
    training = [views[i] for i in (0, 1, 3, 4)]
    held_cache = ray_candidates(model, views[2].origins, views[2].directions)
    held = replace(views[2], candidates=held_cache.indices, overflow=held_cache.overflow)
    truth = torch.tensor(reference['ranges'][2], dtype=model.means.dtype, device=device)
    started = time.perf_counter()
    with torch.no_grad():
        initial = model.render(held.origins, held.directions, held.candidates)
        before = (initial['median_depth']-truth).abs()
    caches = [None]*len(training)
    for step in range(steps):
        index = step%len(training); view = training[index]
        if caches[index] is None or not caches[index].covers(model, view.origins, view.directions):
            caches[index] = ray_candidates(model, view.origins, view.directions)
        optimizer.step(replace(view, candidates=caches[index].indices, overflow=caches[index].overflow))
    with torch.no_grad():
        held_cache = ray_candidates(model, held.origins, held.directions)
        held = replace(held, candidates=held_cache.indices, overflow=held_cache.overflow)
        result = model.render(held.origins, held.directions, held.candidates)
        error = (result['median_depth']-truth).abs()
        position = held.origins+torch.nn.functional.normalize(held.directions, dim=-1)*result['median_depth'][:, None]
        normal = torch.tensor(reference['true_plane_normal'], device=device, dtype=model.means.dtype)
        point = torch.tensor(reference['true_plane_point'], device=device, dtype=model.means.dtype)
        surface_error = abs(((position-point)*normal).sum(-1))
        qualified = (result['alpha'] >= .5) & ~held.overflow
        report = {'schema': 'sfnp-surface-gaussian-control-v1', 'test_only': True,
            'orientation': orientation, 'steps': steps, 'device': device,
            'normal_seed_noise_radians': normal_noise_radians, 'normals_optimized': optimize_normals,
            'training_views': 4, 'held_out_views': 1, 'head_translation_m': .64,
            'initial_seed_rays_and_colors_from_training_images_only': True,
            'held_out_candidate_overflow_rays': int(held.overflow.sum()),
            'initial_seed_bias_m': .12, 'held_out_before_depth_median_error_m': float(before.median()),
            'held_out_depth_median_error_m': float(error.median()),
            'held_out_depth_p95_error_m': float(torch.quantile(error, .95)),
            'held_out_surface_median_error_m': float(surface_error.median()),
            'held_out_surface_p95_error_m': float(torch.quantile(surface_error, .95)),
            'held_out_photometric_mae': float((result['color']-held.colors).abs().mean()),
            'held_out_supported_ray_fraction': float(qualified.float().mean()),
            'wall_seconds': time.perf_counter()-started,
            'reference_geometry_used_as_optimization_prior': False,
            'held_out_image_used_in_optimization': False,
            'ground_truth_alignment_fitted': False, 'headset_capture': False,
            'real_room_accuracy_verified': False, 'nvidia_throughput_verified': False,
            'accepted_for_live_installation': False}
        report['synthetic_control_passed'] = bool(error.quantile(.95) < .05 and qualified.all())
    return report, model


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--orientation', choices=('front', 'floor', 'ceiling'), default='front')
    parser.add_argument('--steps', type=int, default=3000)
    parser.add_argument('--normal-noise', type=float, default=0.)
    parser.add_argument('--optimize-normals', action='store_true')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Choose a new private evidence directory')
    torch.set_num_threads(4)
    report, model = run_planar_control(args.orientation, steps=args.steps,
        normal_noise_radians=args.normal_noise, optimize_normals=args.optimize_normals)
    args.output.mkdir(mode=0o700, parents=True)
    (args.output/'report.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    np.savez_compressed(args.output/'model.npz', **{k: v.detach().cpu().numpy() for k, v in model.state_dict().items()})
    print(json.dumps(report, allow_nan=False))


if __name__ == '__main__':
    main()
