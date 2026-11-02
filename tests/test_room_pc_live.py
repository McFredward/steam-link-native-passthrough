"""Live room transport, measured geometry and Gaussian refinement regressions."""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from room_scan.pc_relay import PcRelay, CHUNK
from room_scan.pc_reconstruction import PcReconstruction
from room_scan.capture import Binding
from room_scan.geometry import RoomError

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("room_setup_helper", ROOT/"setup/steam_link_setup_room.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


@unittest.skipIf(os.name == "nt", "Frame-side Unix relay is tested on Linux")
class RelayTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.relay = PcRelay(Path(self.temporary.name)/"relay")
        self.addCleanup(self.relay.close)
        self.binding = {"session_id": "00000000-0000-0000-0000-000000000001", "origin_id": "00000000-0000-0000-0000-000000000002", "calibration_id": "00000000-0000-0000-0000-000000000003"}

    def test_latest_capture_lease_chunks_and_epoch_cancel(self):
        relay = self.relay
        self.assertRaises(RoomError, relay._dispatch, {"op": "take"})
        relay._dispatch({"op": "ready", "protocol": 1, "provider": "CUDAExecutionProvider"})
        self.assertIsNone(relay.pending)
        relay.set_scan(self.binding, 4, "scan", "room-a")
        for number in range(3):
            relay.offer({"binding": self.binding, "capture_epoch": 4, "pixels": "a"*300000, "sequence": number})
        lease = relay._dispatch({"op": "take"})
        self.assertEqual(lease["id"], 3)
        raw = bytearray()
        for offset in range(0, lease["bytes"], CHUNK):
            reply = relay._dispatch({"op": "chunk", "id": lease["id"], "offset": offset})
            self.assertLess(len(json.dumps(reply)), 128*1024)
            raw.extend(base64.b64decode(reply["data"]))
        self.assertEqual(json.loads(raw)["sequence"], 2)
        model = json.dumps({"binding": self.binding, "capture_epoch": 4}).encode()
        relay._dispatch({"op": "begin", "bytes": len(model), "sha256": hashlib.sha256(model).hexdigest(),
                         "binding": self.binding, "epoch": 4})
        relay.set_scan(None, 5, "idle")
        self.assertRaises(RoomError, relay._dispatch, {"op": "put", "offset": 0, "data": base64.b64encode(model).decode()})
        self.assertTrue(relay.events.empty())
        self.assertIsNone(relay.lease)

    def test_private_socket_refuses_cpu_and_bad_digest(self):
        relay = self.relay
        self.assertRaises(RoomError, relay._dispatch, {"op": "ready", "protocol": 1, "provider": "CPUExecutionProvider"})
        relay._dispatch({"op": "ready", "protocol": 1, "provider": "CUDAExecutionProvider"})
        relay.set_scan(self.binding, 0, "scan", "room-a")
        relay._dispatch({"op": "begin", "bytes": 2, "sha256": "0"*64, "binding": self.binding, "epoch": 0})
        relay._dispatch({"op": "put", "offset": 0, "data": base64.b64encode(b"{}").decode()})
        self.assertRaises(RoomError, relay._dispatch, {"op": "commit"})
        self.assertTrue(relay.events.empty())

    def test_renderer_model_validation_and_stale_results(self):
        binding = Binding(**self.binding, test_only=False)
        reconstruction = PcReconstruction(binding, self.temporary.name)
        self.addCleanup(reconstruction.close)
        record = struct.pack("<I11f", 12, 0, 0, -2, .02, 0, 0, 0, .02, 0, .6, 1)
        result = {"event": "pc_model", "sequence": 1, "binding": self.binding, "gaussians": base64.b64encode(record).decode(), "face_count": 0}
        reconstruction.accept(result)
        self.assertEqual(reconstruction.path.read_bytes(), record)
        self.assertEqual(reconstruction.path.stat().st_mode & 0o777, 0o600)
        self.assertRaises(RoomError, reconstruction.accept, result)
        result = {**result, "sequence": 2, "gaussians": base64.b64encode(record+record).decode()}
        self.assertRaises(RoomError, reconstruction.accept, result)
        self.assertEqual(reconstruction.path.read_bytes(), record)
        result["gaussians"] = base64.b64encode(struct.pack("<I11f", 12, 0, 0, -2, .02, 0, 0, .02, 0, 0, .6, 1)).decode()
        self.assertRaises(RoomError, reconstruction.accept, result)

    def test_application_finish_semantics_save_and_close_reopen(self):
        from room_scan.app import Application
        from room_scan.store import RoomStore
        from room_scan.workflow import Stage
        app = Application(RoomStore(Path(self.temporary.name)/"rooms"),
                          reconstruction_factory=lambda binding: PcReconstruction(binding, self.temporary.name))
        app.pc_relay = self.relay
        app.action("new")
        app.event({"event": "camera_ready", "binding": self.binding})
        record = struct.pack("<I11f", 0, 0, 0, -2, .02, 0, 0, 0, .02, 0, .6, 1)
        app.event({"event": "pc_model", "binding": self.binding, "capture_epoch": 0, "sequence": 1,
                   "gaussians": base64.b64encode(record).decode(), "face_count": 1})
        self.assertTrue(app.view()["gaussian_file"])
        self.assertTrue(app.session.view()["can_review"])
        app.action("review")
        self.assertEqual(app.session.stage, Stage.PAUSED)
        self.assertEqual(app.pc_pending_action, "review")
        app.event({"event": "pc_mesh", "binding": self.binding, "capture_epoch": 0, "sequence": 2,
                   "mesh": {"vertices": [[0, 0, -2], [.1, 0, -2], [0, .1, -2]], "triangles": [[0, 1, 2]],
                            "quality": ["uncertain"]*3}})
        self.assertEqual(app.page, "review")
        app.action("add")
        self.assertEqual(app.page, "types")
        app.action("back")
        app.action("save")
        self.assertEqual(app.page, "saved")
        self.assertTrue(list(app.store.root.glob("*.head")))
        app.event({"event": "close", "capture_epoch": 1})
        app.action("new")
        app.event({"event": "pc_model", "binding": self.binding, "capture_epoch": 0, "sequence": 10,
                   "gaussians": base64.b64encode(record).decode(), "face_count": 1})
        self.assertIsNone(app.session)


@unittest.skipUnless(importlib.util.find_spec("numpy") and importlib.util.find_spec("scipy"), "PC numerical dependencies required")
class GeometryTests(unittest.TestCase):
    def test_metric_stereo_rays_and_real_holes(self):
        import numpy as np
        from room_reconstruction.live_geometry import triangulate
        h, w, f, baseline = 64, 96, 80., .06
        y, x = np.mgrid[:h, :w]
        rays = np.stack(((x-(w-1)/2)/f, -(y-(h-1)/2)/f, -np.ones_like(x)), -1)
        left = np.zeros((h, w, 3))
        right = left.copy()
        right[..., 0] = baseline
        disparity = np.full((h, w), f*baseline/2)
        mask = np.ones((h, w), bool)
        mask[20:30, 40:50] = False
        room = np.column_stack((np.eye(3), np.zeros(3)))
        points, normals, valid, depth = triangulate((disparity, disparity), (mask, mask), (left, right), rays, room, (f, f, 47.5, 31.5))
        self.assertGreater(valid.sum(), 4000)
        self.assertLess(np.max(np.abs(depth[valid]-2)), 1e-9)
        self.assertFalse(valid[20:30, 40:50].any())
        self.assertTrue(np.all(normals[..., 2][valid] > .99))
        bad = disparity+4
        self.assertFalse(triangulate((disparity, bad), (mask, mask), (left, right), rays, room, (f, f, 47.5, 31.5))[2].any())

    def test_revisits_refine_gaussians_then_extract_measured_polygons(self):
        import numpy as np
        from room_reconstruction.live_map import GaussianMap
        h, w, focal = 64, 96, 80.
        y, x = np.mgrid[:h, :w]
        points = np.stack(((x-47.5)*2/focal, -(y-31.5)*2/focal, -np.full_like(x, 2.)), -1)
        normals = np.zeros_like(points)
        normals[..., 2] = 1
        valid = np.ones((h, w), bool)
        valid[20:40, 30:60] = False
        model = GaussianMap(10000, step=4)
        biased = points.copy()
        biased[..., 2] -= .02
        model.integrate(biased, normals, valid, np.full((h, w), 2.02), np.zeros(3), focal)
        initial = model.positions.copy()
        for i in range(30):
            model.integrate(points, normals, valid, np.full((h, w), 2.), np.array((i*.03, 0., 0.)), focal)
        self.assertLess(float(np.median(np.abs(model.positions[:, 2]+2))), .004)
        self.assertGreater(float(np.median(np.abs(initial[:, 2]+2))), .019)
        self.assertTrue(np.any(model.quality() == 2))
        preview = base64.b64decode(model.preview())
        self.assertEqual(len(preview), len(model.positions)*48)
        mesh = model.mesh()
        self.assertGreater(len(mesh["triangles"]), 100)
        vertices = np.asarray(mesh["vertices"])
        centers = vertices[np.asarray(mesh["triangles"])].mean(axis=1)
        # No triangle bridges the intentionally unobserved central patch.
        self.assertFalse(((np.abs(centers[:, 0]) < .20) & (np.abs(centers[:, 1]) < .15)).any())
        restored = GaussianMap(10000, step=4)
        restored.restore(model.preview())
        self.assertTrue(np.allclose(restored.positions, model.positions))
        self.assertEqual(int(restored.active.sum()), len(model.positions))
        self.assertGreater(len(restored.mesh()["triangles"]), 100)

    def test_row_history_refuses_extrapolation_and_reflections(self):
        import numpy as np
        from room_reconstruction.live_geometry import matrix, row_poses
        pose = np.column_stack((np.eye(3), np.zeros(3))).ravel().tolist()
        samples = [{"raw_ns": 100+i*100, "query_ns": 100, "head": pose} for i in range(9)]
        rotation, position = row_poses(samples, np.array((100, 500, 900)))
        self.assertTrue(np.allclose(rotation, np.eye(3)))
        self.assertRaises(ValueError, row_poses, samples, np.array((99, 500)))
        reflected = np.column_stack((np.diag((-1, 1, 1)), np.zeros(3)))
        self.assertRaises(ValueError, matrix, reflected.ravel())


if __name__ == "__main__":
    unittest.main()
