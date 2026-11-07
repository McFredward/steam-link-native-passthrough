"""Bounded, differentiable ray/surface Gaussian rendering for developer trials.

The model is a set of oriented two-dimensional Gaussian disks, not volumetric
densities. Ray origins/directions may include the exact fisheye and rolling-row
camera model. PyTorch operators execute on the selected CPU/CUDA device without
a compiler or custom extension. This module does not grant camera authority,
install a runtime, publish a room, or claim installed reconstruction acceptance.

Geometry follows the ray/splat surface model in Huang et al., 2DGS (2024):
https://surfsplatting.github.io/ . This is an independent implementation.
"""
from __future__ import annotations

import torch
from torch import nn
from torch.nn import functional as F

MAX_RAYS = 4096
MAX_CANDIDATES = 512
MAX_RAY_CANDIDATE_PAIRS = 524288


def bounded_ray_work(p, k):
    """Trade batch size for overlap capacity without growing tensor memory."""
    return 0 < p <= MAX_RAYS and 0 < k <= MAX_CANDIDATES and p*k <= MAX_RAY_CANDIDATE_PAIRS


def tangent_basis(normals):
    normals = F.normalize(normals, dim=-1)
    reference = F.one_hot(normals.abs().argmin(-1), 3).to(normals.dtype)
    u = F.normalize(torch.linalg.cross(reference, normals), dim=-1)
    v = torch.linalg.cross(normals, u)
    return torch.stack((u, v), -2)


class SurfaceGaussians(nn.Module):
    """Metric surface parameters; the caller owns bounded view/candidate storage."""

    def __init__(self, means, normals, scales, colors, opacity=.8):
        super().__init__()
        means = torch.as_tensor(means)
        if means.dtype not in (torch.float32, torch.float64):
            means = means.float()
        n = len(means)
        convert = lambda value: torch.as_tensor(value, device=means.device, dtype=means.dtype)
        normals, scales, colors = map(convert, (normals, scales, colors))
        opacity = torch.broadcast_to(convert(opacity), (n,)).clone()
        if (not 0 < n <= 200000 or means.shape != (n, 3) or normals.shape != (n, 3)
                or scales.shape != (n, 2) or colors.ndim != 2 or colors.shape[0] != n
                or not 1 <= colors.shape[1] <= 3
                or not all(torch.isfinite(v).all() for v in (means, normals, scales, colors, opacity))
                or torch.any(torch.linalg.vector_norm(normals, dim=-1) < .9)
                or torch.any(scales <= 0) or torch.any(scales > 1.)
                or torch.any((colors < 0) | (colors > 1)) or torch.any((opacity <= 0) | (opacity >= 1))):
            raise ValueError('Invalid bounded metric surface Gaussian initialization')
        self.means = nn.Parameter(means.clone())
        self.axes = nn.Parameter(tangent_basis(normals))
        self.log_scales = nn.Parameter(scales.log())
        self.color_logits = nn.Parameter(torch.logit(colors.clamp(1e-5, 1-1e-5)))
        self.opacity_logits = nn.Parameter(torch.logit(opacity))

    def basis(self, ids=None):
        axes = self.axes if ids is None else self.axes[ids]
        u = F.normalize(axes[..., 0, :], dim=-1)
        v = F.normalize(axes[..., 1, :]-(axes[..., 1, :]*u).sum(-1, keepdim=True)*u, dim=-1)
        return u, v, torch.linalg.cross(u, v)

    def render(self, origins, directions, candidates, *, near=.2, far=6., background=0.,
               minimum_training_transmittance=0.):
        """Composite exact ray/disk intersections, in per-ray depth order.

        Candidate -1 means no disk. Truncation/overflow must be reported by the
        candidate producer, never disguised as qualified empty space. A batch
        is explicitly bounded independently of map size. No screen-facing disk
        or virtual room plane substitutes for a missing surface intersection.
        """
        p, k = candidates.shape
        if (not bounded_ray_work(p, k) or candidates.dtype != torch.int64
                or origins.shape != (p, 3) or directions.shape != (p, 3)
                or not .01 <= near < far <= 100 or not 0 <= minimum_training_transmittance <= .05):
            raise ValueError('Ray/surface rendering exceeds its explicit batch budget')
        if candidates.device != self.means.device or origins.device != self.means.device or directions.device != self.means.device:
            raise ValueError('Ray/surface tensors must share their selected device')
        if torch.any(candidates < -1) or torch.any(candidates >= len(self.means)):
            raise ValueError('Invalid surface candidate index')
        ids = candidates.clamp_min(0)
        u, v, normals = self.basis(ids)
        means = self.means[ids]
        direction = F.normalize(directions, dim=-1)[:, None]
        origin = origins[:, None]
        denominator = (direction*normals).sum(-1)
        signed_safe = torch.where(denominator >= 0, denominator.clamp_min(1e-6), denominator.clamp_max(-1e-6))
        distance = ((means-origin)*normals).sum(-1)/signed_safe
        displacement = origin+distance[..., None]*direction-means
        scale = self.log_scales[ids].exp()
        uv = torch.stack(((displacement*u).sum(-1), (displacement*v).sum(-1)), -1)/scale
        radius2 = uv.square().sum(-1)
        present = (candidates >= 0) & (denominator.abs() > 1e-6) & (distance >= near) & (distance <= far) & (radius2 <= 18.)
        alpha = torch.where(present, self.opacity_logits[ids].sigmoid()*(-.5*radius2).exp(), 0.).clamp_max(.999)
        order = torch.argsort(torch.where(present, distance, torch.inf), dim=-1)
        alpha = alpha.gather(1, order)
        depth = torch.where(present, distance, 0.).gather(1, order)
        color = self.color_logits[ids].sigmoid().gather(1, order[..., None].expand(-1, -1, self.color_logits.shape[1]))
        # Two-sided surface normals face the actual observation ray.
        facing = normals*torch.where(denominator < 0, 1., -1.)[..., None]
        facing = facing.gather(1, order[..., None].expand(-1, -1, 3))
        transparency = 1-alpha
        prefix = torch.cat((torch.ones_like(alpha[:, :1]), transparency[:, :-1]), 1).cumprod(1)
        if minimum_training_transmittance and torch.is_grad_enabled():
            # Stop hidden ray/disk pairs from training geometry or appearance.
            # A disk can remain visible in a different ray in this same batch;
            # a per-disk update mask alone cannot isolate these gradients.
            visible = (prefix.detach() >= minimum_training_transmittance).to(alpha.dtype)
            for value in (alpha, depth, color, facing):
                if value.requires_grad:
                    mask = visible[..., None] if value.ndim == 3 else visible
                    value.register_hook(lambda gradient, mask=mask: gradient*mask)
        weights = prefix*alpha
        coverage = weights.sum(1)
        rendered = (weights[..., None]*color).sum(1)+(1-coverage)[:, None]*background
        expected_depth = (weights*depth).sum(1)/coverage.clamp_min(1e-8)
        accumulated = weights.cumsum(1)
        median_index = (accumulated < .5).sum(1).clamp_max(k-1)
        median_depth = depth.gather(1, median_index[:, None])[:, 0]
        median_depth = torch.where(coverage >= .5, median_depth, torch.zeros_like(median_depth))
        weighted_depth = weights*depth
        before_w = weights.cumsum(1)-weights
        before_wd = weighted_depth.cumsum(1)-weighted_depth
        # Equivalent to sum_ij w_i*w_j*abs(t_i-t_j), without a K*K tensor.
        distortion = (2*weights*(depth*before_w-before_wd)).sum(1).clamp_min(0)
        normal = (weights[..., None]*facing).sum(1)/coverage[:, None].clamp_min(1e-8)
        normal_variance = (coverage-torch.linalg.vector_norm((weights[..., None]*facing).sum(1), dim=-1)).clamp_min(0)
        return {'color': rendered, 'alpha': coverage, 'depth': expected_depth,
                'median_depth': median_depth, 'normal': normal,
                'distortion': distortion, 'normal_variance': normal_variance,
                'weights': weights, 'intersection_depth': depth,
                'transmittance': prefix,
                'candidate_ids': candidates.gather(1, order)}
