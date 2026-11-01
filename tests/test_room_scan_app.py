"""Owned native-pipe/controller-flow fixtures, not a camera or headset test."""
from dataclasses import replace
from contextlib import redirect_stderr
import io
import json
from pathlib import Path
import tempfile
import unittest

from room_scan.app import Application, LABELS, run
from room_scan.capture import Binding
from room_scan.geometry import RoomError
from room_scan.model import Label
from room_scan.store import RoomStore
from room_scan.workflow import Stage

IDS = [f"00000000-0000-4000-8000-{i:012d}" for i in range(1, 6)]
BINDING = dict(session_id=IDS[0], origin_id=IDS[1], calibration_id=IDS[2])


class AppTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.store = RoomStore(self.temporary.name)
        self.app = Application(self.store)

    def start(self):
        self.app.action("new")
        self.app.event(dict(event="camera_warming"))
        self.assertFalse(self.app.view()["buttons"][0]["enabled"])
        self.app.event(dict(event="camera_ready", binding=BINDING))

    def depth(self):
        self.app.event(dict(event="depth", binding=BINDING, sequence=1, exposure_ticks=100,
            pose=dict(position=[0, 0, 0], orientation_xyzw=[0, 0, 0, 1]), width=3, height=3,
            intrinsics=[20, 20, 1, 1], depth_m=[1]*9, confidence=[1]*9))

    def test_default_dashboard_does_not_acquire_or_animate_a_scan(self):
        view = self.app.view()
        self.assertEqual("home", view["page"])
        self.assertFalse(view["capture_active"])
        self.assertIsNone(view["room"])
        self.app.action("new")
        self.app.event(dict(event="camera_unavailable"))
        self.assertEqual("prepare", self.app.page)
        self.assertFalse(self.app.view()["capture_active"])

    def test_controller_measurement_confirm_review_save_query(self):
        self.start()
        self.depth()
        self.app.action("add")
        actions = {b["action"] for b in self.app.view()["buttons"]}
        self.assertTrue({"type:"+label.value for label in LABELS} <= actions)
        self.app.action("type:TABLE")
        self.app.action("measure_controller")
        for n, point in enumerate(((0, .8, 0), (2, .8, 0), (2, .8, -1)), 1):
            self.app.event(dict(event="point", origin_id=IDS[1], sample_ns=n,
                                controller_position=point, ray_origin=[0, 0, 0], ray_direction=[0, 0, -1]))
        self.assertEqual("Table", self.app.pending.name)
        self.assertFalse(self.app.pending.confirmed)
        self.app.action("depth_more")
        self.assertAlmostEqual(.85, self.app.pending.geometry.box_size[2])
        self.app.action("confirm_element")
        self.app.action("review")
        self.app.event(dict(event="rename", name="Wohnzimmer"))
        self.app.action("save")
        saved = self.app.session.room
        self.assertEqual("saved", self.app.page)
        self.assertFalse(self.app.view()["capture_active"])
        self.assertEqual("Wohnzimmer", self.store.load(saved.id).name)
        self.assertEqual(1, len(self.store.query(saved.id, Label.TABLE, current_origin_id=IDS[1])))

    def test_bad_origin_cannot_create_an_element(self):
        self.start()
        self.app.action("type:TABLE")
        self.app.action("measure_controller")
        with self.assertRaises(RoomError):
            self.app.event(dict(event="point", origin_id=IDS[4], sample_ns=1, controller_position=[0, 0, 0]))
        self.assertFalse(self.app.draft.points)

    def test_camera_refusal_has_a_visible_diagnostic_code(self):
        self.app.action("new")
        self.app.event(dict(event="camera_warming"))
        self.app.event(dict(event="camera_unavailable", reason="camera-count-unsupported"))
        view=self.app.view()
        self.assertIn("Diagnostic code: camera-count-unsupported",view["notice"])
        self.assertFalse(view["camera_check_pending"])
        self.assertFalse(view["capture_active"])

    def test_invalid_events_log_bounded_metadata_and_keep_controller_alive(self):
        incoming=io.BytesIO(b'{"event":"unknown"}\n'*70)
        outgoing=io.BytesIO();errors=io.StringIO()
        with redirect_stderr(errors):run(self.store.root,incoming,outgoing)
        self.assertEqual(64,len(errors.getvalue().splitlines()))
        states=[json.loads(line) for line in outgoing.getvalue().splitlines()]
        self.assertEqual(71,len(states));self.assertTrue(states[-1]["notice"])

    def test_saved_room_is_explicitly_realigned_and_revisioned(self):
        self.test_controller_measurement_confirm_review_save_query()
        original = self.app.session.room
        self.app.action("load:"+original.id)
        self.assertFalse(self.app.view()["room"]["world_visuals_allowed"])
        self.app.event(dict(event="camera_ready", binding={**BINDING, "origin_id": IDS[4]}))
        self.app.measure_with_controller = True
        for n, point in enumerate(((1, .8, -1), (3, .8, -1), (3, .8, -2)), 1):
            self.app.event(dict(event="point", origin_id=IDS[4], sample_ns=n, controller_position=point))
        self.assertIsNotNone(self.app.aligned_preview)
        self.app.action("confirm_alignment")
        self.app.action("continue")
        self.assertTrue(self.app.session.reconstruction.mesh().triangles)
        self.app.session.tracking_verified = True
        self.app.action("review")
        self.app.action("save")
        saved = self.store.load(original.id)
        self.assertEqual(2, saved.revision)
        self.assertEqual(IDS[4], saved.origin_id)
        self.assertEqual(original.elements[0].id, saved.elements[0].id)
        self.assertAlmostEqual(saved.elements[0].pose.position[0]-original.elements[0].pose.position[0], 1)
        self.assertAlmostEqual(saved.elements[0].pose.position[2]-original.elements[0].pose.position[2], -1)

    def test_edit_from_review_returns_to_review_and_names_are_editable(self):
        self.test_controller_measurement_confirm_review_save_query()
        self.app.session.stage = Stage.REVIEW
        self.app.session.tracking_verified = True
        element = self.app.session.room.elements[0]
        self.app.action("elements")
        self.app.action("element:"+element.id)
        self.app.event(dict(event="rename_element", name="Esstisch"))
        self.app.action("back")
        self.assertEqual("review", self.app.page)
        self.assertEqual("Esstisch", self.app.session.room.elements[0].name)

    def test_tracking_loss_needs_native_recheck_and_new_source_epoch(self):
        self.start()
        self.depth()
        before = self.app.session.reconstruction.mesh()
        self.app.event(dict(event="tracking_lost"))
        self.assertIsNone(self.app.view()["room"]["mesh"])
        with self.assertRaises(RoomError):
            self.app.action("resume")
        renewed = {**BINDING, "session_id": IDS[3]}
        self.app.event(dict(event="camera_resumed", binding=renewed, tracking_valid=True))
        self.assertEqual(Stage.SCANNING, self.app.session.stage)
        self.assertEqual(before, self.app.session.reconstruction.mesh())
        self.assertEqual(0, self.app.session.reconstruction.sequence)

    def test_cancel_preserves_measured_geometry_in_separate_draft(self):
        self.start()
        self.depth()
        self.app.action("cancel")
        self.assertFalse(list(self.store.root.glob("*.head")))
        drafts = list(self.store.root.glob("draft.*.json"))
        self.assertEqual(1, len(drafts))
        self.assertTrue(json.loads(drafts[0].read_text())["room"]["mesh"]["triangles"])

    def test_pipe_eof_closes_owned_session_and_emits_valid_states(self):
        incoming = io.BytesIO(b'{"event":"action","action":"new"}\n{"event":"camera_unavailable"}\n')
        outgoing = io.BytesIO()
        run(self.store.root, incoming, outgoing)
        states = [json.loads(line) for line in outgoing.getvalue().splitlines()]
        self.assertEqual(["home", "prepare", "prepare"], [s["page"] for s in states])
        self.assertTrue(all(not s["capture_active"] for s in states))


if __name__ == "__main__":
    unittest.main()
