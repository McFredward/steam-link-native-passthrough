"""Validate PC Gaussian results before exposing them to the native MR renderer."""
from __future__ import annotations

import base64
import os
from pathlib import Path
import struct
import tempfile

from .capture import Reconstruction
from .geometry import Mesh, require


class PcReconstruction(Reconstruction):
    def __init__(self, binding, root):
        super().__init__(binding)
        self.root = Path(root)
        self.path = None
        self.count = self.reliable = self.faces = 0
        self._mesh = Mesh()

    def accept(self, result):
        require(result["binding"] == {"session_id": self.binding.session_id, "origin_id": self.binding.origin_id,
                "calibration_id": self.binding.calibration_id}, "PC result belongs to another capture")
        sequence = result["sequence"]
        require(type(sequence) is int and self.sequence < sequence < 2**63, "Stale PC model")
        if result["event"] == "pc_mesh":
            mesh = Mesh.from_dict(result["mesh"])
            require(bool(mesh.triangles), "No measured polygon surfaces")
            self._mesh = mesh
        else:
            require(result["event"] == "pc_model" and type(result["gaussians"]) is str
                    and len(result["gaussians"]) <= 5120000, "Invalid PC model")
            raw = base64.b64decode(result["gaussians"], validate=True)
            require(0 < len(raw) <= 80000*48 and len(raw) % 48 == 0, "Gaussian model exceeds budget")
            faces = result["face_count"]
            require(type(faces) is int and 0 <= faces <= 160000, "Invalid measured connectivity")
            seen, reliable = set(), 0
            for record in struct.iter_unpack("<I11f", raw):
                index, *v = record
                require(index < 80000 and index not in seen and all(abs(x) <= 1000 for x in v)
                        and all(x == x for x in v), "Invalid Gaussian record")
                seen.add(index)
                u, w = sum(x*x for x in v[3:6]), sum(x*x for x in v[6:9])
                require(.000001 <= u <= .023 and .000001 <= w <= .023
                        and abs(sum(a*b for a, b in zip(v[3:6], v[6:9]))) < (u*w)**.5*.02
                        and 0 < v[9] <= 1 and v[10] in (1, 2), "Invalid Gaussian covariance")
                reliable += v[10] == 2
            fd, name = tempfile.mkstemp(prefix=".pc-gaussians-", dir=self.root)
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(raw)
                if self.path:
                    os.replace(name, self.path)
                else:
                    self.path = Path(name)
            except BaseException:
                Path(name).unlink(missing_ok=True)
                raise
            self.count, self.reliable = len(seen), reliable
            self._mesh = Mesh()
            self.faces = faces
        self.sequence = sequence
        self.accepted_frames += 1

    def mesh(self):
        return self._mesh

    def status(self):
        return {"accepted_frames": self.accepted_frames, "reliable_cells": self.reliable,
                "uncertain_cells": self.count-self.reliable, "gaussians": self.count,
                "measured_faces": self.faces, "backend": "pc-neural-gaussian"}

    def retain_aligned_mesh(self, mesh):
        self._mesh = mesh

    def close(self):
        if self.path:
            self.path.unlink(missing_ok=True)
            self.path = None
