"""Validate PC Gaussian results before exposing them to the native MR renderer."""
from __future__ import annotations

import base64
import os
from pathlib import Path
import struct
import tempfile

from .capture import Reconstruction
from .geometry import Mesh, RoomError, require


class PcResultError(RoomError):
    """Fixed public reasons; never expose remote payload contents in a notice."""
    messages = {
        "binding": "PC preview belongs to another capture",
        "sequence": "PC preview is out of date",
        "model": "PC preview has an invalid format or size",
        "connectivity": "PC preview has invalid measured connectivity",
        "record": "PC preview has an invalid Gaussian record",
        "covariance": "PC preview has an invalid Gaussian shape",
    }

    def __init__(self, code):
        self.code = "pc-preview-"+code
        super().__init__(self.messages[code])


def checked(condition, code):
    if not condition:
        raise PcResultError(code)


class PcReconstruction(Reconstruction):
    def __init__(self, binding, root):
        super().__init__(binding)
        self.root = Path(root)
        self.path = None
        self.count = self.reliable = self.faces = 0
        self._mesh = Mesh()

    def accept(self, result):
        checked(result["binding"] == {"session_id": self.binding.session_id, "origin_id": self.binding.origin_id,
                "calibration_id": self.binding.calibration_id}, "binding")
        sequence = result["sequence"]
        checked(type(sequence) is int and self.sequence < sequence < 2**63, "sequence")
        if result["event"] == "pc_mesh":
            mesh = Mesh.from_dict(result["mesh"])
            require(bool(mesh.triangles), "No measured polygon surfaces")
            self._mesh = mesh
        else:
            checked(result["event"] == "pc_model" and type(result["gaussians"]) is str
                    and len(result["gaussians"]) <= 5120000, "model")
            raw = base64.b64decode(result["gaussians"], validate=True)
            checked(0 < len(raw) <= 80000*48 and len(raw) % 48 == 0, "model")
            faces = result["face_count"]
            checked(type(faces) is int and 0 <= faces <= 160000, "connectivity")
            seen, reliable = set(), 0
            minimum, maximum = (.001*(1-1e-5))**2, (.15*(1+1e-5))**2
            for record in struct.iter_unpack("<I11f", raw):
                index, *v = record
                checked(index < 80000 and index not in seen and all(abs(x) <= 1000 for x in v)
                        and all(x == x for x in v), "record")
                seen.add(index)
                u, w = sum(x*x for x in v[3:6]), sum(x*x for x in v[6:9])
                # Unit tangent axes multiplied by the 1 mm floor are encoded
                # as float32. Rounding can put their squared length a few ulps
                # below 1e-6; that is still the same valid measured surface.
                checked(minimum <= u <= maximum and minimum <= w <= maximum
                        and abs(sum(a*b for a, b in zip(v[3:6], v[6:9]))) < (u*w)**.5*.02
                        and 0 < v[9] <= 1 and v[10] in (1, 2), "covariance")
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
