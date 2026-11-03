"""Live room transport, measured geometry and Gaussian refinement regressions."""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import time
import unittest
from unittest import mock

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
        app.event({"event": "camera_ready", "binding": self.binding, "capture_epoch": 1})
        app.event({"event": "pc_model", "binding": self.binding, "capture_epoch": 1, "sequence": 1,
                   "gaussians": base64.b64encode(record).decode(), "face_count": 1})
        app.action("review")
        self.assertEqual(app.pc_pending_action, "review")
        self.assertTrue(all(not button["enabled"] for button in app.view()["buttons"]))
        app.event({"event": "close", "capture_epoch": 2})
        self.assertIsNone(app.pc_pending_action)
        self.assertFalse(app.view()["capture_collecting"])
        self.assertEqual(app.page, "home")

    def test_owned_pc_companion_transfers_chunks_and_stops_its_worker(self):
        import subprocess
        import sys
        script = Path(self.temporary.name)/"fixture_gpu.py"
        script.write_text('''import sys,json,base64,struct
print(json.dumps({'event':'ready','provider':'CUDAExecutionProvider','protocol':1}),flush=True)
for raw in sys.stdin:
 m=json.loads(raw);o=m['observation']
 record=struct.pack('<I11f',0,0,0,-2,.02,0,0,0,.02,0,.6,1)
 print(json.dumps({'event':'pc_model','binding':o['binding'],'capture_epoch':o['capture_epoch'],
  'sequence':1,'gaussians':base64.b64encode(record).decode(),'face_count':0}),flush=True)
''')
        original_popen = subprocess.Popen
        children, ui_events = [], []
        class UI:
            def say(self, value):
                ui_events.append(value)
        def child(command, **kwargs):
            self.assertIn("runpy.run_module", command[3])
            kwargs["cwd"] = self.temporary.name
            process = original_popen([sys.executable, "-B", str(script)], **kwargs)
            children.append(process)
            return process
        relay = self.relay
        relay.set_scan(self.binding, 3, "scan", "room-a")
        with (mock.patch.object(sys, "platform", "win32"),
              mock.patch.object(helper.subprocess, "CREATE_NO_WINDOW", 0, create=True),
              mock.patch.object(helper.subprocess, "run", return_value=type("Gpu", (), {"stdout": "20000\n"})()),
              mock.patch.object(helper.subprocess, "Popen", side_effect=child),
              mock.patch.object(helper, "prepare_runtime", return_value=Path(self.temporary.name))):
            companion = helper.Companion(relay._dispatch, ROOT/"setup", Path(self.temporary.name), UI())
            try:
                deadline = time.monotonic()+5
                while not relay.ready and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue(relay.ready)
                self.assertTrue(companion.wait_ready())
                relay.offer({"binding": self.binding, "capture_epoch": 3, "pixels": "x"*200000, "sequence": 1})
                while relay.events.empty() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertFalse(relay.events.empty())
                result = relay.events.get_nowait()
                self.assertEqual(result["event"], "pc_model")
                self.assertEqual(result["binding"], self.binding)
            finally:
                companion.close()
        self.assertEqual(len(children), 1)
        self.assertIsNotNone(children[0].poll())
        self.assertFalse(relay.ready)
        self.assertIn("bridge_room_pc_ready", ui_events)
        self.assertNotIn("bridge_room_pc_failed", ui_events)


@unittest.skipUnless(importlib.util.find_spec("numpy") and importlib.util.find_spec("scipy"), "PC numerical dependencies required")
class GeometryTests(unittest.TestCase):
    def test_float32_gaussians_at_size_floor_survive_frame_contract(self):
        import numpy as np
        from room_reconstruction.live_map import GaussianMap
        rng = np.random.default_rng(137)
        model = GaussianMap(10000)
        model.positions = rng.uniform(-2, 2, (5000, 3)).astype(np.float32)
        normal = rng.normal(size=(5000, 3))
        model.normals = (normal/np.linalg.norm(normal, axis=1)[:, None]).astype(np.float32)
        model.sigma = np.full(5000, .001, np.float32)
        model.active = np.ones(5000, bool)
        model.views, model.residuals = np.ones(5000, np.uint8), np.zeros(5000, np.float32)
        records = list(struct.iter_unpack('<I11f', base64.b64decode(model.preview())))
        self.assertTrue(any(sum(x*x for x in record[4:7]) < 1e-6 for record in records))
        binding = Binding('00000000-0000-0000-0000-000000000001',
                          '00000000-0000-0000-0000-000000000002',
                          '00000000-0000-0000-0000-000000000003', test_only=False)
        with tempfile.TemporaryDirectory() as root:
            reconstruction = PcReconstruction(binding, root)
            self.addCleanup(reconstruction.close)
            value = {'event':'pc_model', 'sequence':1, 'binding':{k:getattr(binding,k) for k in
                     ('session_id','origin_id','calibration_id')}, 'gaussians':model.preview(), 'face_count':0}
            reconstruction.accept(value)
            self.assertEqual(reconstruction.count, 5000)
            before = reconstruction.path.read_bytes()
            model.sigma[0] = .0009
            with self.assertRaisesRegex(RoomError, 'invalid Gaussian shape'):
                reconstruction.accept({**value, 'sequence':2, 'gaussians':model.preview()})
            self.assertEqual(reconstruction.path.read_bytes(), before)
            self.assertEqual(reconstruction.sequence, 1)

    def test_continuous_moving_views_extend_map_close_observed_gap_and_preserve_unknown_gap(self):
        import numpy as np
        from room_reconstruction.live_map import GaussianMap
        model = GaussianMap(10000, step=4)
        h, w, focal = 64, 96, 80.
        y, x = np.mgrid[:h, :w]
        ray = np.stack(((x-47.5)/focal, -(y-31.5)/focal, -np.ones_like(x)), -1)
        binding = Binding('00000000-0000-0000-0000-000000000001',
                          '00000000-0000-0000-0000-000000000002',
                          '00000000-0000-0000-0000-000000000003', test_only=False)
        with tempfile.TemporaryDirectory() as root:
            reconstruction = PcReconstruction(binding, root)
            self.addCleanup(reconstruction.close)
            initial_count = 0
            for i in range(18):
                theta = -.12+i*.014
                rotation = np.array(((np.cos(theta),0,np.sin(theta)),(0,1,0),(-np.sin(theta),0,np.cos(theta))))
                camera = np.array((i*.025, 0, 0))
                direction = ray @ rotation.T
                points = camera+direction*(-2/direction[...,2])[...,None]
                local = (points-camera) @ rotation
                normals = np.zeros_like(points);normals[...,2] = 1
                closing = (np.abs(points[...,0]) < .15) & (np.abs(points[...,1]) < .15)
                unknown = (np.abs(points[...,0]-.7) < .12) & (np.abs(points[...,1]-.3) < .12)
                valid = ~unknown if i else ~(unknown|closing)
                model.integrate(points,normals,valid,-local[...,2],camera,focal,np.column_stack((rotation,camera)))
                if i == 0:
                    initial_count = len(model.positions)
                    self.assertFalse(((np.abs(model.positions[:,0])<.15)&(np.abs(model.positions[:,1])<.15)).any())
                reconstruction.accept({'event':'pc_model','sequence':i+1,
                    'binding':{k:getattr(binding,k) for k in ('session_id','origin_id','calibration_id')},
                    'gaussians':model.preview(),'face_count':len(model.faces)})
            self.assertEqual(reconstruction.accepted_frames,18)
            self.assertGreater(len(model.positions),initial_count)
            self.assertTrue(((np.abs(model.positions[:,0])<.15)&(np.abs(model.positions[:,1])<.15)).any())
            self.assertFalse(((np.abs(model.positions[:,0]-.7)<.12)&(np.abs(model.positions[:,1]-.3)<.12)).any())
            self.assertLess(float(np.max(np.abs(model.positions[:,2]+2))),1e-5)
            self.assertTrue((model.quality()==2).any())

    def test_raw_stereo_rectification_keeps_row_exposure_origins(self):
        import numpy as np
        from room_reconstruction.live_geometry import rectify
        eye = lambda x: np.column_stack((np.eye(3), (x, 0, 0))).ravel().tolist()
        first, last = 1000000000, 1000000000+2463*4360
        poses = [{"raw_ns": first+(last-first)*i//8, "query_ns": 100, "head": eye(i*.001)} for i in range(9)]
        rectified = np.column_stack((np.diag((1, -1, -1)), (-.03, 0, 0))).ravel().tolist()
        image = base64.b64encode(bytes([116])*(616*616)).decode()
        timing = {"first_row_ns": first, "line_ns": 4360, "integration_ns": 1000000}
        observation = {"schema": "sfnp-room-stereo-v1", "encoding": "gray8-point-step4", "width": 616, "height": 616,
            "reference_head": eye(.004), "images": [image, image], "poses": poses, "timing": [timing, timing],
            "rig": {"baseline_m": .06, "head_from_rectified": rectified, "eyes": [
                {"intrinsics": [1000, 1000, 1232, 1232], "k": [0, 0, 0, 0], "head_from_optical": eye(x)} for x in (-.03, .03)]}}
        images, masks, origins, rays, room, intrinsics = rectify(observation, 320, 256)
        self.assertGreater(int(masks[0].sum()), 70000)
        self.assertEqual(int(images[0][128, 160]), 116)
        self.assertAlmostEqual(float(origins[1][128, 160, 0]-origins[0][128, 160, 0]), .06, places=8)
        self.assertGreater(float(origins[0][230, 160, 0]-origins[0][25, 160, 0]), .001)
        self.assertAlmostEqual(float(room[0, 3]), -.026, places=8)
        self.assertTrue(np.isfinite(rays).all())

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
