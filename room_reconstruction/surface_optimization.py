"""Bounded image optimization of metric 2D Gaussians; no capture or live grants."""
from __future__ import annotations

from dataclasses import dataclass
import math

import torch
from .surface_gaussians import bounded_ray_work


@dataclass(frozen=True)
class RayTrainingView:
    origins: torch.Tensor
    directions: torch.Tensor
    colors: torch.Tensor
    candidates: torch.Tensor
    valid: torch.Tensor
    overflow: torch.Tensor
    surface_neighbors: torch.Tensor | None = None
    measured_depth: torch.Tensor | None = None
    measured_sigma: torch.Tensor | None = None

    def validate(self, model):
        p = len(self.origins)
        if (self.origins.shape != (p, 3) or self.directions.shape != (p, 3)
                or self.colors.shape != (p, model.color_logits.shape[1])
                or self.candidates.ndim != 2 or self.candidates.shape[0] != p
                or not bounded_ray_work(p, self.candidates.shape[1]) or self.candidates.dtype != torch.int64
                or self.valid.shape != (p,) or self.overflow.shape != (p,)
                or self.valid.dtype != torch.bool or self.overflow.dtype != torch.bool
                or any(value.device != model.means.device for value in (
                    self.origins, self.directions, self.colors, self.candidates, self.valid, self.overflow))
                or any(value.dtype != model.means.dtype for value in (self.origins, self.directions, self.colors))
                or not all(torch.isfinite(value).all() for value in (self.origins, self.directions, self.colors))
                or torch.any(torch.linalg.vector_norm(self.directions, dim=-1) < .9)
                or torch.any((self.colors < 0) | (self.colors > 1))):
            raise ValueError('Invalid bounded camera-ray training view')
        if self.surface_neighbors is not None:
            links = self.surface_neighbors
            if (links.ndim != 2 or links.shape[1] != 3 or len(links) > p
                    or links.dtype != torch.int64 or links.device != self.origins.device
                    or torch.any(links < 0) or torch.any(links >= p)):
                raise ValueError('Invalid bounded measured-surface neighbour links')
        if (self.measured_depth is None) != (self.measured_sigma is None):
            raise ValueError('Metric depth constraints need their measured uncertainty')
        if self.measured_depth is not None:
            depth, sigma = self.measured_depth, self.measured_sigma
            if (any(v.shape != (p,) or v.dtype != self.origins.dtype or v.device != self.origins.device
                    or not torch.isfinite(v).all() for v in (depth, sigma))
                    or torch.any(~(((depth == 0) & (sigma == 0))
                                  | ((depth >= .2) & (depth <= 6.) & (sigma >= .005) & (sigma <= .1))))):
                raise ValueError('Invalid measured metric depth constraints')


class SurfaceOptimizer:
    """Update only visible disks, preserving inactive geometry and Adam state.

    A dense optimizer can keep moving an invisible disk through its old momentum.
    Per-disk counters and moment updates avoid both that drift and global Adam
    bias-correction jumps when an old room region re-enters the camera view.
    The caller schedules views and retains independent immutable previews.
    """

    def __init__(self, model, *, max_displacement_m=.3, optimize_normals=True, random_background=True,
                 seed_directions=None):
        if not .01 <= max_displacement_m <= .5:
            raise ValueError('Invalid surface optimization displacement budget')
        self.model = model
        self.seed_means = model.means.detach().clone()
        self.seed_normals = model.basis()[2].detach().clone()
        self.seed_directions = self.seed_normals
        self.tangent_weight = .01
        if seed_directions is not None:
            value = torch.as_tensor(seed_directions, dtype=model.means.dtype, device=model.means.device)
            if value.shape != model.means.shape or not torch.isfinite(value).all() or torch.any(torch.linalg.vector_norm(value, dim=-1) < .9):
                raise ValueError('Invalid metric seed-ray directions')
            self.seed_directions = torch.nn.functional.normalize(value, dim=-1).detach().clone()
            # A depth proposal can move along its measured camera ray. Its
            # lateral localization is much stronger evidence than its depth;
            # retain a soft one-centimetre transverse uncertainty, not a plane.
            self.tangent_weight = 100.
        self.max_displacement_m = max_displacement_m
        self.optimize_normals = optimize_normals
        self.random_background = random_background
        self.generator = torch.Generator(device=model.means.device).manual_seed(732019)
        self.steps = 0
        self.counts = torch.zeros(len(model.means), dtype=torch.int64, device=model.means.device)
        self.rates = {'means': .002, 'axes': .0005, 'log_scales': .001,
                      'color_logits': .02, 'opacity_logits': .005}
        self.moments = {name: (torch.zeros_like(p), torch.zeros_like(p)) for name, p in model.named_parameters()}

    def step(self, view):
        view.validate(self.model)
        eligible = view.valid & ~view.overflow & (view.candidates >= 0).any(-1)
        if not eligible.any():
            return {'updated': False, 'disks_updated': 0, 'rays_used': 0,
                    'metric_constraint_rays': 0,
                    'candidate_overflow_rays': int(view.overflow.sum())}
        self.model.zero_grad(set_to_none=True)
        rendered = self.model.render(view.origins[eligible], view.directions[eligible], view.candidates[eligible])
        ids = rendered['candidate_ids'][(rendered['weights'] > 1e-5) & (rendered['candidate_ids'] >= 0)].unique()
        if not len(ids):
            return {'updated': False, 'disks_updated': 0, 'rays_used': 0,
                    'metric_constraint_rays': 0,
                    'candidate_overflow_rays': int(view.overflow.sum())}
        color = rendered['color']
        if self.random_background:
            # An opaque measured surface must predict the same captured image
            # regardless of an artificial background behind its splats. This
            # prevents dark colors and low opacity from disguising a gap.
            background = torch.rand((), device=color.device, dtype=color.dtype, generator=self.generator)
            color = color+(1-rendered['alpha'])[:, None]*background
        image_loss = ((color-view.colors[eligible]).square()+1e-5).sqrt().mean()
        shift = self.model.means[ids]-self.seed_means[ids]
        tangent_shift = shift-(shift*self.seed_directions[ids]).sum(-1, keepdim=True)*self.seed_directions[ids]
        loss = (image_loss+.1*rendered['distortion'].mean()
                +.01*rendered['normal_variance'].mean()+.01*(1-rendered['alpha']).mean()
                +self.tangent_weight*tangent_shift.square().mean())
        metric_rays = 0
        if view.measured_depth is not None:
            # These are independent metric observations from training exposures,
            # e.g. qualified multi-view feature triangulation. The caller must
            # not insert a neural proposal, a reference room dimension, held-out
            # geometry or this model's previous prediction as a measurement.
            measured, sigma = view.measured_depth[eligible], view.measured_sigma[eligible]
            known = measured > 0
            metric_rays = int(known.sum())
            if metric_rays:
                residual = (rendered['depth'][known]-measured[known])/sigma[known]
                loss = loss+.01*(residual.square()+1e-5).sqrt().mean()
        if view.surface_neighbors is not None and self.steps >= 100:
            # Surface normals derived from the rendered metric depth must agree
            # with the disks' normals. Links come only from caller-qualified
            # local continuity; an unknown opening or object edge gets no link.
            remap = torch.full_like(view.valid, -1, dtype=torch.int64)
            remap[eligible] = torch.arange(int(eligible.sum()), device=remap.device)
            links = remap[view.surface_neighbors]
            links = links[(links >= 0).all(-1)]
            if len(links):
                links = links[(rendered['alpha'][links] >= .5).all(-1)]
            if len(links):
                points = view.origins[eligible]+torch.nn.functional.normalize(view.directions[eligible], dim=-1)*rendered['depth'][:, None]
                normal = torch.linalg.cross(points[links[:, 1]]-points[links[:, 0]], points[links[:, 2]]-points[links[:, 0]])
                usable = torch.linalg.vector_norm(normal, dim=-1) > 1e-8
                normal = torch.nn.functional.normalize(normal[usable], dim=-1)
                predicted_normal = torch.nn.functional.normalize(rendered['normal'][links[usable, 0]], dim=-1)
                if len(normal):
                    loss = loss+.05*(1-abs((normal*predicted_normal).sum(-1))).clamp_min(0).mean()
        loss.backward()
        # No partial parameter/momentum update is allowed after invalid gradients.
        if not torch.isfinite(loss) or any(p.grad is not None and not torch.isfinite(p.grad[ids]).all()
                                          for p in self.model.parameters()):
            raise ValueError('Surface optimization produced nonfinite gradients')
        with torch.no_grad():
            self.counts[ids] += 1
            self.steps += 1
            for name, parameter in self.model.named_parameters():
                if parameter.grad is None or name == 'axes' and not self.optimize_normals:
                    continue
                selected = ids[self.counts[ids] > 100] if name == 'axes' else ids
                if not len(selected):
                    continue
                count = self.counts[selected].to(self.model.means.dtype)
                gradient = parameter.grad[selected].clamp(-100., 100.)
                first, second = self.moments[name]
                first[selected] = .9*first[selected]+.1*gradient
                second[selected] = .999*second[selected]+.001*gradient.square()
                dims = (len(selected),)+(1,)*(parameter.ndim-1)
                # Normal updates have a per-disk warmup; their moment clock
                # starts when their first actual update is permitted.
                if name == 'axes':
                    count = count-100
                first_hat = first[selected]/(1-.9**count).reshape(dims)
                second_hat = second[selected]/(1-.999**count).reshape(dims)
                change = -self.rates[name]*first_hat/(second_hat.sqrt()+1e-8)
                parameter.index_add_(0, selected, change)
            displacement = self.model.means[ids]-self.seed_means[ids]
            scale = (self.max_displacement_m/torch.linalg.vector_norm(displacement, dim=-1).clamp_min(1e-12)).clamp_max(1)
            self.model.means[ids] = self.seed_means[ids]+displacement*scale[:, None]
            self.model.log_scales[ids] = self.model.log_scales[ids].clamp(math.log(.005), math.log(.35))
            self.model.opacity_logits[ids] = self.model.opacity_logits[ids].clamp(-4.6, 8.)
            self.model.color_logits[ids] = self.model.color_logits[ids].clamp(-12., 12.)
            if self.optimize_normals:
                u, v, _ = self.model.basis(ids)
                self.model.axes[ids] = torch.stack((u, v), 1)
        return {'updated': True, 'disks_updated': len(ids), 'rays_used': int(eligible.sum()),
                'metric_constraint_rays': metric_rays,
                'candidate_overflow_rays': int(view.overflow.sum()),
                'photometric_loss': float(image_loss.detach()), 'loss': float(loss.detach())}
