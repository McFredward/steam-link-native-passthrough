"""Conservative bounded ray/sphere culling for rolling-row/fisheye 2DGS.

The candidate search uses the actual world ray of each pixel. It requires no
global-shutter approximation, rectified camera or screen-space ellipse. Memory
is bounded by the explicit ray/point chunks and per-ray capacity, never N*P.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

import torch
from torch.nn import functional as F


@dataclass(frozen=True)
class RayCandidateSet:
    indices: torch.Tensor
    counts: torch.Tensor
    overflow: torch.Tensor
    reference_means: torch.Tensor
    reference_radii: torch.Tensor
    reference_origins: torch.Tensor
    reference_directions: torch.Tensor
    motion_margin_m: float
    far_m: float

    @torch.no_grad()
    def covers(self, model, origins, directions):
        if model.means.shape != self.reference_means.shape or origins.shape != self.reference_origins.shape:
            return False
        displacement = torch.linalg.vector_norm(model.means-self.reference_means, dim=-1)
        radius_growth = (math.sqrt(18)*model.log_scales.exp().amax(-1)-self.reference_radii).clamp_min(0)
        origin_motion = torch.linalg.vector_norm(origins-self.reference_origins, dim=-1).amax()
        angle_motion = torch.linalg.vector_norm(F.normalize(directions, dim=-1)-self.reference_directions, dim=-1).amax()
        return bool((displacement+radius_growth).amax()+origin_motion+self.far_m*angle_motion <= self.motion_margin_m)


@torch.no_grad()
def ray_candidates(model, origins, directions, *, capacity=128, ray_chunk=256, point_chunk=512,
                   motion_margin_m=.04, near=.2, far=6.):
    p = len(origins); n = len(model.means)
    if (origins.shape != (p, 3) or directions.shape != (p, 3) or not 0 < p <= 4096
            or not 0 < capacity <= 128 or not 0 < ray_chunk <= 512 or not 0 < point_chunk <= 1024
            or not 0 <= motion_margin_m <= .1 or not .01 <= near < far <= 100
            or origins.device != model.means.device or directions.device != model.means.device
            or origins.dtype != model.means.dtype or directions.dtype != model.means.dtype
            or not torch.isfinite(origins).all() or not torch.isfinite(directions).all()
            or torch.any(torch.linalg.vector_norm(directions, dim=-1) < .9)):
        raise ValueError('Invalid bounded world-ray candidate search')
    directions = F.normalize(directions, dim=-1)
    radii = math.sqrt(18)*model.log_scales.exp().amax(-1)
    padded = radii+motion_margin_m+1e-4
    indices = torch.full((p, capacity), -1, dtype=torch.int64, device=origins.device)
    counts = torch.zeros(p, dtype=torch.int64, device=origins.device)
    for start in range(0, p, ray_chunk):
        end = min(p, start+ray_chunk)
        selected_distance = torch.full((end-start, capacity), torch.inf, dtype=origins.dtype, device=origins.device)
        selected_indices = indices[start:end]
        for begin in range(0, n, point_chunk):
            finish = min(n, begin+point_chunk)
            delta = model.means[None, begin:finish]-origins[start:end, None]
            distance = (delta*directions[start:end, None]).sum(-1)
            perpendicular = (delta.square().sum(-1)-distance.square()).clamp_min(0)
            radius = padded[None, begin:finish]
            present = ((perpendicular <= radius.square()) & (distance+radius >= near)
                       & (distance-radius <= far))
            counts[start:end] += present.sum(-1)
            keys = torch.cat((selected_distance, torch.where(present, distance, torch.inf)), 1)
            ids = torch.arange(begin, finish, device=origins.device).expand(end-start, -1)
            all_indices = torch.cat((selected_indices, ids), 1)
            selected_distance, order = keys.topk(capacity, dim=-1, largest=False, sorted=False)
            selected_indices = all_indices.gather(1, order)
        indices[start:end] = torch.where(torch.isfinite(selected_distance), selected_indices, -1)
    return RayCandidateSet(indices, counts, counts > capacity, model.means.detach().clone(),
                           radii.clone(), origins.clone(), directions.clone(), motion_margin_m, far)
