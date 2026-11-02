"""Owned bounded Open3D-derived TSDF module; no runtime dependency downloads."""
from __future__ import annotations

import ctypes as c
import os
from pathlib import Path
import platform
import stat
import struct
import weakref

from .capture import Binding, CaptureCapacityError, DepthFrame, Reconstruction
from .geometry import Mesh, require


def load_library(path):
    path = Path(path).absolute()
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        header = os.pread(fd, 64, 0)
        machine = 183 if platform.machine() == "aarch64" else 62 if platform.machine() == "x86_64" else 0
        require(stat.S_ISREG(info.st_mode) and info.st_uid in (0, os.getuid()) and not info.st_mode & 0o022
                and 64 <= info.st_size <= 32*1024*1024 and len(header) == 64
                and header[:7] == b"\x7fELF\x02\x01\x01" and machine
                and struct.unpack_from("<HH", header, 16) == (3, machine), "Untrusted native room vision module")
        library = c.CDLL(str(path), mode=os.RTLD_LOCAL | os.RTLD_NOW)
        after = path.lstat()
        require((info.st_dev, info.st_ino) == (after.st_dev, after.st_ino), "Native room vision module changed")
    finally:
        os.close(fd)
    signatures = {
        "sfnp_room_vision_abi": (c.c_uint32, ()),
        "sfnp_room_vision_error": (c.c_char_p, ()),
        "sfnp_fusion_create": (c.c_void_p, (c.c_float, c.c_uint32, c.c_uint32, c.c_uint32)),
        "sfnp_fusion_destroy": (None, (c.c_void_p,)),
        "sfnp_fusion_integrate": (c.c_int, (c.c_void_p, c.c_uint32, c.c_uint32, c.c_double, c.c_double,
            c.c_double, c.c_double, c.POINTER(c.c_float), c.POINTER(c.c_float), c.POINTER(c.c_double), c.c_uint64)),
        "sfnp_fusion_rebind": (c.c_int, (c.c_void_p,)),
        "sfnp_fusion_mesh_budget": (c.c_int, (c.c_void_p, c.c_uint32, c.c_uint32)),
        "sfnp_fusion_mesh_size": (c.c_int, (c.c_void_p, c.POINTER(c.c_uint32), c.POINTER(c.c_uint32))),
        "sfnp_fusion_copy_mesh": (c.c_int, (c.c_void_p, c.POINTER(c.c_float), c.c_uint32,
            c.POINTER(c.c_uint32), c.c_uint32, c.POINTER(c.c_uint8))),
        "sfnp_fusion_block_count": (c.c_uint32, (c.c_void_p,)),
    }
    for name, (restype, argtypes) in signatures.items():
        entry = getattr(library, name)
        entry.restype, entry.argtypes = restype, argtypes
    require(library.sfnp_room_vision_abi() == 1, "Unsupported native room vision ABI")
    return library


class NativeTsdfReconstruction(Reconstruction):
    def __init__(self, binding: Binding, cell_m=.05, max_vertices=100000, max_triangles=200000, *, library=None, max_blocks=2048):
        super().__init__(binding, cell_m, max_vertices, max_triangles)
        require(type(max_blocks) is int and 1 <= max_blocks <= 2048, "Invalid TSDF volume limit")
        self._library = load_library(library or Path(__file__).resolve().parent.parent / "libsfnp-room-vision.so")
        context = self._library.sfnp_fusion_create(cell_m, max_blocks, max_vertices, max_triangles)
        require(context, "Native room fusion could not start")
        self._context = context
        self._cleanup = weakref.finalize(self, self._library.sfnp_fusion_destroy, context)
        self._mesh_cache = Mesh()

    def _check(self, result):
        error = self._library.sfnp_room_vision_error()
        reason = error.decode("utf-8", "replace")[:160] if error else "Native room fusion failed"
        if result < 0:
            limits = {"Scan vertex budget reached": "vertices", "Scan triangle budget reached": "triangles",
                      "Scan volume budget reached": "volume", "Scan exposure budget reached": "exposures"}
            if reason in limits:
                raise CaptureCapacityError(limits[reason])
        require(result >= 0, reason)
        return result

    def integrate(self, frame: DepthFrame):
        require(self._context and isinstance(frame, DepthFrame) and frame.binding == self.binding,
                "Capture origin/calibration/session changed")
        require(frame.tracking_valid, "Tracking lost; scan must pause")
        require(frame.sequence > self.sequence and frame.exposure_ticks > self.exposure_ticks, "Stale exposure")
        require(self.accepted_frames < 60000, "Scan exposure budget reached")
        pose = frame.camera_to_room
        columns = tuple(pose.rotate(axis) for axis in ((1., 0., 0.), (0., 1., 0.), (0., 0., 1.)))
        matrix = (c.c_double*12)(*(v for r in range(3) for v in (*[col[r] for col in columns], pose.position[r])))
        depth = (c.c_float*len(frame.depth_m))(*frame.depth_m)
        confidence = (c.c_float*len(frame.confidence))(*frame.confidence)
        count = self._check(self._library.sfnp_fusion_integrate(self._context, frame.width, frame.height,
            frame.fx, frame.fy, frame.cx, frame.cy, depth, confidence, matrix, frame.sequence))
        self.sequence, self.exposure_ticks = frame.sequence, frame.exposure_ticks
        self.accepted_frames += 1
        self._mesh_cache = None
        return count

    def rebind(self, binding: Binding):
        require(self._context, "Native fusion is closed")
        require(binding.origin_id == self.binding.origin_id and binding.calibration_id == self.binding.calibration_id
                and binding.test_only == self.binding.test_only, "Changed room origin/calibration requires alignment")
        self._check(self._library.sfnp_fusion_rebind(self._context))
        super().rebind(binding)

    def retain_aligned_mesh(self, mesh: Mesh):
        require(isinstance(mesh, Mesh) and not self.base_mesh.vertices, "Aligned base mesh already set")
        remaining_vertices = self.max_vertices-len(mesh.vertices)
        remaining_triangles = self.max_triangles-len(mesh.triangles)
        require(remaining_vertices >= 1 and remaining_triangles >= 1, "Aligned geometry budget reached")
        self._check(self._library.sfnp_fusion_mesh_budget(self._context, remaining_vertices, remaining_triangles))
        super().retain_aligned_mesh(mesh)

    def mesh(self):
        if self._mesh_cache is not None:
            return self._mesh_cache
        require(self._context, "Native fusion is closed")
        vertices, triangles = c.c_uint32(), c.c_uint32()
        self._check(self._library.sfnp_fusion_mesh_size(self._context, c.byref(vertices), c.byref(triangles)))
        require(vertices.value+len(self.base_mesh.vertices) <= self.max_vertices
                and triangles.value+len(self.base_mesh.triangles) <= self.max_triangles, "Native mesh budget differs")
        xyz, faces, quality = (c.c_float*(3*vertices.value))(), (c.c_uint32*(3*triangles.value))(), (c.c_uint8*vertices.value)()
        self._check(self._library.sfnp_fusion_copy_mesh(self._context, xyz, vertices, faces, triangles, quality))
        base = self.base_mesh
        points = tuple(tuple(xyz[i:i+3]) for i in range(0, len(xyz), 3))
        indices = tuple(tuple(k+len(base.vertices) for k in faces[i:i+3]) for i in range(0, len(faces), 3))
        labels = ("building", "uncertain", "reliable")
        require(all(q < len(labels) for q in quality), "Invalid native surface quality")
        self._mesh_cache = Mesh(base.vertices+points, base.triangles+indices, base.quality+tuple(labels[q] for q in quality))
        return self._mesh_cache

    def status(self):
        qualities = self.mesh().quality
        return {"observed_cells": len(qualities), "reliable_cells": qualities.count("reliable"),
                "uncertain_cells": qualities.count("uncertain"), "building_cells": qualities.count("building"),
                "accepted_frames": self.accepted_frames, "whole_room_percentage": None}

    def close(self):
        if self._context:
            self.mesh()
            self._cleanup()
            self._context = None
