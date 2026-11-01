"""Bounded incremental reconstruction from exposure-aligned depth observations.

This module is a consumer, NOT an Arcturus camera driver. There is deliberately
no default or synthetic live provider. Optical depth is positive forward;
camera_to_room uses XR axes. Unknown pixels never create observed geometry.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

from .geometry import Mesh, Pose, distance, dot, number, require
from .model import identifier


@dataclass(frozen=True)
class Binding:
    session_id: str
    origin_id: str
    calibration_id: str
    camera_family: str = "arcturus"
    test_only: bool = True

    def __post_init__(self):
        for value in (self.session_id, self.origin_id, self.calibration_id):
            identifier(value)
        require(self.camera_family == "arcturus" and type(self.test_only) is bool, "Arcturus capture binding required")


@dataclass(frozen=True)
class DepthFrame:
    binding: Binding
    sequence: int
    exposure_ticks: int
    camera_to_room: Pose
    width: int
    height: int
    # Intrinsics refer to this exact rectified depth grid, not full-resolution RGB.
    fx: float
    fy: float
    cx: float
    cy: float
    depth_m: tuple[float, ...]
    confidence: tuple[float, ...]
    tracking_valid: bool = True

    def __post_init__(self):
        require(isinstance(self.binding, Binding) and isinstance(self.camera_to_room, Pose), "Invalid depth binding")
        require(type(self.sequence) is int and 0 < self.sequence < 2**63
                and type(self.exposure_ticks) is int and 0 < self.exposure_ticks < 2**63, "Invalid exposure identity")
        require(type(self.width) is int and type(self.height) is int and 2 <= self.width <= 160
                and 2 <= self.height <= 120, "Depth grid budget exceeded")
        require(number(self.fx, 100000) > 0 and number(self.fy, 100000) > 0
                and 0 <= number(self.cx) < self.width and 0 <= number(self.cy) < self.height, "Invalid depth intrinsics")
        require(len(self.depth_m) == len(self.confidence) == self.width * self.height, "Depth extent differs")
        require(type(self.tracking_valid) is bool, "Invalid tracking flag")
        # Zero represents unavailable depth; nonfinite/negative input is malformed.
        depths = tuple(number(x, 100.) for x in self.depth_m)
        confidences = tuple(number(x, 1.) for x in self.confidence)
        require(all(x >= 0 for x in depths) and all(0 <= x <= 1 for x in confidences), "Invalid depth sample")
        object.__setattr__(self, "depth_m", depths)
        object.__setattr__(self, "confidence", confidences)


def depth_from_rectified_disparity(disparity, fx, baseline_m):
    """Triangulate a calibrated rectified stereo pair; no inferred metric scale."""
    require(number(fx, 100000) > 0 and .001 <= number(baseline_m, 1.) <= 1., "Invalid stereo calibration")
    require(len(disparity) <= 160 * 120, "Disparity budget exceeded")
    result = []
    for value in disparity:
        d = number(value, 100000)
        result.append(fx * baseline_m / d if d > 0 else 0.)
    return tuple(result)


@dataclass(frozen=True)
class Cell:
    point: tuple[float, float, float]
    weight: float
    samples: int
    viewpoints: tuple[tuple[float, float, float], ...]
    max_error: float = 0.

    @property
    def quality(self):
        if self.max_error > .04 or self.weight / self.samples < .7:
            return "uncertain"
        return "reliable" if len(self.viewpoints) >= 3 else "building"


class Reconstruction:
    def __init__(self, binding: Binding, cell_m=.03, max_vertices=100000, max_triangles=200000):
        require(isinstance(binding, Binding), "Capture binding required")
        require(.01 <= number(cell_m, 1.) <= .10 and type(max_vertices) is int and 1 <= max_vertices <= 100000
                and type(max_triangles) is int and 1 <= max_triangles <= 200000, "Invalid reconstruction limits")
        self.binding = binding
        self.cell_m = cell_m
        self.max_vertices, self.max_triangles = max_vertices, max_triangles
        self.cells = {}
        self.triangles = set()
        self.base_mesh = Mesh()
        self.sequence = self.exposure_ticks = self.accepted_frames = 0

    def integrate(self, frame: DepthFrame):
        require(isinstance(frame, DepthFrame) and frame.binding == self.binding, "Capture origin/calibration/session changed")
        require(frame.tracking_valid, "Tracking lost; scan must pause")
        require(frame.sequence > self.sequence and frame.exposure_ticks > self.exposure_ticks, "Stale exposure")
        require(self.accepted_frames < 60000, "Scan exposure budget reached")
        observations, grid = {}, {}
        for i, (z, confidence) in enumerate(zip(frame.depth_m, frame.confidence)):
            if not .25 <= z <= 6. or confidence < .4:
                continue
            u, v = i % frame.width, i // frame.width
            point = frame.camera_to_room.transform(((u-frame.cx)*z/frame.fx, -(v-frame.cy)*z/frame.fy, -z))
            require(all(abs(p) <= 1000 and math.isfinite(p) for p in point), "Depth outside room limits")
            key = tuple(math.floor(p / self.cell_m) for p in point)
            grid[i] = (key, z)
            # One best sample per cell/exposure: dense pixels do not count as
            # independent observations or multiple camera viewpoints.
            if key not in observations or confidence > observations[key][1]:
                observations[key] = (point, confidence)
        require(len(self.cells.keys() | observations.keys()) + len(self.base_mesh.vertices) <= self.max_vertices, "Scan geometry budget reached")
        changes = {}
        viewpoint = frame.camera_to_room.position
        for key, (point, confidence) in observations.items():
            old = self.cells.get(key)
            if old is None:
                changes[key] = Cell(point, confidence, 1, (viewpoint,))
                continue
            weight = old.weight + confidence
            mean = tuple((a*old.weight+b*confidence)/weight for a, b in zip(old.point, point))
            views = old.viewpoints
            if len(views) < 3 and all(distance(viewpoint, p) >= .05 for p in views):
                views += (viewpoint,)
            changes[key] = Cell(mean, weight, old.samples+1, views, max(old.max_error, distance(old.point, point)))
        added = set()
        for v in range(frame.height-1):
            for u in range(frame.width-1):
                a = v*frame.width+u
                for indices in ((a, a+frame.width, a+1), (a+1, a+frame.width, a+frame.width+1)):
                    if not all(i in grid for i in indices):
                        continue
                    keys = tuple(grid[i][0] for i in indices)
                    if len(set(keys)) != 3:
                        continue
                    depths = [grid[i][1] for i in indices]
                    # Never bridge holes, depth discontinuities, or widely
                    # separated samples into an invented wall/table surface.
                    if max(depths)-min(depths) > .06+.02*min(depths):
                        continue
                    points = [changes[k].point for k in keys]
                    if max(distance(x, y) for x in points for y in points) > .25:
                        continue
                    a_point, b_point, c_point = points
                    ab = tuple(b-a for a, b in zip(a_point, b_point))
                    ac = tuple(c-a for a, c in zip(a_point, c_point))
                    cross = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
                    if dot(cross, cross) > 1e-14:
                        # Canonical cyclic rotation preserves the measured
                        # front face; sorting vertices would flip normals.
                        added.add(min(keys, keys[1:]+keys[:1], keys[2:]+keys[:2]))
        require(len(self.triangles | added) + len(self.base_mesh.triangles) <= self.max_triangles, "Scan triangle budget reached")
        # Commit only after the entire exposure passes capacity/identity checks.
        self.cells.update(changes)
        self.triangles.update(added)
        self.sequence, self.exposure_ticks = frame.sequence, frame.exposure_ticks
        self.accepted_frames += 1
        return len(observations)

    def rebind(self, binding: Binding):
        require(binding.origin_id == self.binding.origin_id and binding.calibration_id == self.binding.calibration_id
                and binding.test_only == self.binding.test_only, "Changed room origin/calibration requires alignment")
        self.binding = binding
        self.sequence = self.exposure_ticks = 0

    def retain_aligned_mesh(self, mesh: Mesh):
        """Retain explicitly aligned measured geometry; never invent view evidence.

        The application calls this only after the user confirms re-registration.
        New observations keep their own cells and quality evidence. Old geometry
        counts against the same total budget and is not silently erased by rescan.
        """
        require(isinstance(mesh, Mesh) and not self.base_mesh.vertices, "Aligned base mesh already set")
        require(len(mesh.vertices)+len(self.cells) <= self.max_vertices
                and len(mesh.triangles)+len(self.triangles) <= self.max_triangles, "Aligned geometry budget reached")
        self.base_mesh = mesh

    def mesh(self):
        keys = sorted(self.cells)
        indices = {k: i for i, k in enumerate(keys)}
        base = self.base_mesh
        return Mesh(base.vertices + tuple(self.cells[k].point for k in keys),
                    base.triangles + tuple(tuple(indices[k]+len(base.vertices) for k in t) for t in sorted(self.triangles)),
                    base.quality + tuple(self.cells[k].quality for k in keys))

    def status(self):
        qualities = list(self.base_mesh.quality) + [cell.quality for cell in self.cells.values()]
        return {"observed_cells": len(qualities), "reliable_cells": qualities.count("reliable"),
                "uncertain_cells": qualities.count("uncertain"), "building_cells": qualities.count("building"),
                "accepted_frames": self.accepted_frames, "whole_room_percentage": None}


class ArcturusProviderUnavailable:
    """Production default until a calibrated live source has been established."""
    reason = "Der Live-Zugriff auf die Arcturus-Kamera ist noch nicht verfügbar."

    def open(self):
        raise RuntimeError(self.reason)

    def close(self):
        pass
