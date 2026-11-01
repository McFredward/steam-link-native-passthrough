"""Software evidence only: no native camera/overlay/headset is exercised here."""
from dataclasses import replace
import json
import math
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from room_scan.capture import Binding, DepthFrame, Reconstruction, depth_from_rectified_disparity
from room_scan.geometry import Mesh, Pose, RoomError, polygon
from room_scan.editor import SurfaceDraft, pick_surface
from room_scan.model import Element, Geometry, Label, Room
from room_scan.store import RoomStore, decode, encode
from room_scan.workflow import ScanSession, Stage

ROOM = "00000000-0000-4000-8000-000000000001"
ORIGIN = "00000000-0000-4000-8000-000000000002"
CALIBRATION = "00000000-0000-4000-8000-000000000003"
SESSION = "00000000-0000-4000-8000-000000000004"
WALL = "00000000-0000-4000-8000-000000000005"
WINDOW = "00000000-0000-4000-8000-000000000006"
TABLE = "00000000-0000-4000-8000-000000000007"
OTHER = "00000000-0000-4000-8000-000000000008"
RECT = ((-1., -1.), (1., -1.), (1., 1.), (-1., 1.))
HORIZONTAL = Pose((0., .8, -1.), (-math.sqrt(.5), 0., 0., math.sqrt(.5)))


def wall():
    return Element(WALL, Label.WALL_FACE, "Wand", Pose(), Geometry(RECT), confirmed=True)


def table():
    return Element(TABLE, Label.TABLE, "Esstisch", HORIZONTAL,
                   Geometry(RECT, (2., 2., .8), (0., 0., -.4)), confirmed=True)


def frame(sequence=1, *, binding=None, x=0., depths=(1.,)*9, confidence=(1.,)*9, tracking_valid=True):
    return DepthFrame(binding or Binding(SESSION, ORIGIN, CALIBRATION), sequence, sequence*1000000,
                      Pose((x, 0., 0.)), 3, 3, 20., 20., 1., 1., depths, confidence, tracking_valid)


def measured_mesh():
    reconstruction = Reconstruction(Binding(SESSION, ORIGIN, CALIBRATION))
    reconstruction.integrate(frame())
    return reconstruction.mesh()


def room(*, elements=(), test_only=True):
    return Room(ROOM, "Wohnzimmer", 0, ORIGIN, CALIBRATION, elements,
                partial_scan_accepted=True, test_only=test_only, mesh=measured_mesh())


class Provider:
    def __init__(self, binding=None):
        self.binding = binding or Binding(SESSION, ORIGIN, CALIBRATION)
        self.closed = False

    def open(self):
        return self.binding

    def close(self):
        self.closed = True


class GeometryTests(unittest.TestCase):
    def test_planes_boxes_and_wall_relationships_roundtrip(self):
        window = Element(WINDOW, Label.WINDOW_FRAME, "Fenster", Pose(),
                         Geometry(tuple((x/2, y/2) for x, y in RECT)), WALL, True)
        model = room(elements=(wall(), window, table()))
        loaded = Room.from_dict(decode(encode(model.to_dict())))
        self.assertEqual(model, loaded)
        self.assertEqual((table(),), loaded.query(Label.TABLE))
        self.assertEqual(WALL, loaded.query(Label.WINDOW_FRAME)[0].parent_wall)
        self.assertAlmostEqual(.8, table().pose.position[1])
        self.assertAlmostEqual(1., table().pose.rotate((0., 0., 1.))[1])

    def test_wall_resize_cannot_orphan_or_move_opening_outside(self):
        window = Element(WINDOW, Label.WINDOW_FRAME, "Fenster", Pose((.5, 0., 0.)),
                         Geometry(tuple((x/4, y/4) for x, y in RECT)), WALL, True)
        model = room(elements=(wall(), window))
        with self.assertRaises(RoomError):
            model.remove(WALL)
        with self.assertRaises(RoomError):
            model.put(replace(wall(), geometry=Geometry(tuple((x/3, y/3) for x, y in RECT))))
        with self.assertRaises(RoomError):
            model.put(replace(window, pose=Pose((0., 0., .5))))
        with self.assertRaises(RoomError):
            model.put(replace(window, pose=Pose((0., 0., 0.), (0., math.sqrt(.5), 0., math.sqrt(.5)))))
        self.assertEqual((wall(),), model.remove(WINDOW).elements)

    def test_bad_geometry_and_unknown_formats(self):
        for boundary in (RECT[::-1], RECT[:2], ((0, 0), (1, 1), (1, 0), (0, 1)), ((0, 0),)*3):
            with self.assertRaises(RoomError):
                polygon(boundary)
        # A regular star has locally positive turns but is self-intersecting.
        circle = [(math.cos(i*2*math.pi/5), math.sin(i*2*math.pi/5)) for i in range(5)]
        with self.assertRaises(RoomError):
            polygon([circle[i] for i in (0, 2, 4, 1, 3)])
        for value in (float("nan"), float("inf"), True):
            with self.assertRaises(RoomError):
                Pose((value, 0., 0.))
        with self.assertRaises(RoomError):
            Pose(orientation=(0., 0., 0., 2.))
        with self.assertRaises(RoomError):
            replace(table(), pose=Pose())
        with self.assertRaises(RoomError):
            Geometry((), (1., 0., 1.), (0., 0., 0.))
        with self.assertRaises(RoomError):
            Element(TABLE, Label.COUCH, "Sofa", Pose(), Geometry(RECT))
        data = room().to_dict()
        data["schema"] = "sfnp-room-v99"
        with self.assertRaises(RoomError):
            Room.from_dict(data)

    def test_pose_inverse_preserves_world_point(self):
        pose = Pose((3., 2., -1.), (0., math.sqrt(.5), 0., math.sqrt(.5)))
        point = (1.2, -2.3, .4)
        for a, b in zip(point, pose.inverse_transform(pose.transform(point))):
            self.assertAlmostEqual(a, b)


class ReconstructionTests(unittest.TestCase):
    def test_calibrated_disparity_has_metric_scale(self):
        self.assertEqual((1.28, 0., 0.), depth_from_rectified_disparity((10., 0., -1.), 200., .064))
        with self.assertRaises(RoomError):
            depth_from_rectified_disparity((10.,), 200., 0.)

    def test_projection_preserves_xr_camera_axes(self):
        reconstruction = Reconstruction(Binding(SESSION, ORIGIN, CALIBRATION))
        reconstruction.integrate(frame(x=1.))
        points = reconstruction.mesh().vertices
        self.assertTrue(all(p[2] == -1. for p in points))
        self.assertIn((.95, .05, -1.), points)
        self.assertIn((1.05, -.05, -1.), points)

    def test_unknown_pixels_and_depth_discontinuities_never_create_mesh(self):
        binding = Binding(SESSION, ORIGIN, CALIBRATION)
        empty = Reconstruction(binding)
        self.assertEqual(0, empty.integrate(frame(depths=(0.,)*9)))
        self.assertFalse(empty.mesh().vertices)
        holes = Reconstruction(binding)
        holes.integrate(frame(depths=(1., 0., 1., 1., 0., 1., 1., 0., 1.)))
        self.assertEqual(6, len(holes.mesh().vertices))
        self.assertFalse(holes.mesh().triangles)
        jumps = Reconstruction(binding)
        jumps.integrate(frame(depths=(1., 2., 1., 2., 1., 2., 1., 2., 1.)))
        self.assertFalse(jumps.mesh().triangles)
        self.assertIsNone(jumps.status()["whole_room_percentage"])

    def test_mesh_winding_preserves_measured_surface_normal(self):
        mesh = measured_mesh()
        for triangle in mesh.triangles:
            a, b, c = (mesh.vertices[i] for i in triangle)
            normal_z = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
            self.assertGreater(normal_z, 0.)

    def test_repeated_view_does_not_establish_reliable_coverage(self):
        reconstruction = Reconstruction(Binding(SESSION, ORIGIN, CALIBRATION))
        for sequence in range(1, 5):
            reconstruction.integrate(frame(sequence))
        self.assertEqual(0, reconstruction.status()["reliable_cells"])
        self.assertEqual(9, reconstruction.status()["observed_cells"])
        # Translate by exact grid-aligned steps. Some sampled world cells now
        # overlap across three distinct camera positions.
        for sequence, x in ((5, .05), (6, .10)):
            reconstruction.integrate(frame(sequence, x=x))
        self.assertGreater(reconstruction.status()["reliable_cells"], 0)

    def test_identity_tracking_and_capacity_fail_before_mutation(self):
        binding = Binding(SESSION, ORIGIN, CALIBRATION)
        reconstruction = Reconstruction(binding)
        reconstruction.integrate(frame())
        before = reconstruction.mesh(), reconstruction.status()
        for bad in (frame(), frame(2, tracking_valid=False), frame(2, binding=replace(binding, origin_id=OTHER)),
                    frame(2, binding=replace(binding, calibration_id=OTHER)),
                    frame(2, binding=replace(binding, session_id=OTHER))):
            with self.assertRaises(RoomError):
                reconstruction.integrate(bad)
            self.assertEqual(before, (reconstruction.mesh(), reconstruction.status()))
        small = Reconstruction(binding, max_vertices=3)
        with self.assertRaises(RoomError):
            small.integrate(frame())
        self.assertFalse(small.cells)
        self.assertEqual(0, small.sequence)
        for bad in (dict(depth_m=(float("nan"),)*9), dict(width=100000), dict(fx=0), dict(confidence=(-.5,)*9)):
            with self.assertRaises(RoomError):
                replace(frame(), **bad)


class FlowTests(unittest.TestCase):
    def test_missing_camera_cannot_animate_or_complete_scan(self):
        session = ScanSession()
        self.assertFalse(session.start())
        view = session.view()
        self.assertEqual("unavailable", view["stage"])
        self.assertIsNone(view["mesh"])
        self.assertIsNone(view["progress"])
        self.assertFalse(view["can_add_element"])
        with self.assertRaises(RoomError):
            session.ingest(frame())

    def test_capture_provider_is_closed_on_cancel_and_failed_alignment(self):
        provider = Provider()
        with ScanSession(provider=provider) as session:
            session.start()
            session.cancel()
        self.assertTrue(provider.closed)
        provider = Provider(Binding(SESSION, OTHER, CALIBRATION))
        session = ScanSession(provider=provider, previous=room())
        self.assertFalse(session.start())
        self.assertTrue(provider.closed)

    def test_scan_edit_review_save_and_semantic_query(self):
        binding = Binding(SESSION, ORIGIN, CALIBRATION, test_only=False)
        session = ScanSession(provider=Provider(binding))
        self.assertTrue(session.start())
        session.ingest(frame(binding=binding))
        session.put_element(wall())
        session.put_element(table())
        session.pause()
        self.assertEqual(2, len(session.view()["elements"]))
        self.assertTrue(session.resume(binding, tracking_valid=True))
        session.review()
        self.assertFalse(session.view()["can_save"])
        session.accept_captured_area()
        self.assertTrue(session.view()["can_save"])
        with tempfile.TemporaryDirectory() as path:
            store = RoomStore(path)
            saved = session.save(store)
            self.assertEqual(1, saved.revision)
            self.assertEqual("saved", session.view()["stage"])
            self.assertTrue(session.provider.closed)
            self.assertEqual((table(),), store.query(saved.id, Label.TABLE, current_origin_id=ORIGIN))
            with self.assertRaises(RoomError):
                store.query(saved.id, Label.TABLE, current_origin_id=OTHER)

    def test_new_tracking_origin_requires_alignment(self):
        session = ScanSession(provider=Provider(Binding(SESSION, OTHER, CALIBRATION)), previous=room())
        self.assertFalse(session.start())
        self.assertEqual(Stage.ALIGNMENT_REQUIRED, session.stage)
        self.assertIsNone(session.reconstruction)
        with self.assertRaises(RoomError):
            session.put_element(table())

    def test_rescan_suggestions_preserve_user_edits(self):
        previous = room(elements=(table(),))
        session = ScanSession(provider=Provider(), previous=previous)
        session.start()
        suggestion = replace(table(), name="Neuer Tisch", confirmed=False, origin="detected")
        self.assertFalse(session.suggest_element(suggestion))
        self.assertEqual("Esstisch", session.room.query(Label.TABLE)[0].name)
        session.ingest(frame())
        session.review()
        session.accept_captured_area()
        self.assertFalse(session.view()["can_save"])
        session.resolve_conflict(TABLE, keep_confirmed=True)
        session.accept_captured_area()
        self.assertTrue(session.view()["can_save"])
        session.put_element(replace(table(), name="Bearbeitet"))
        self.assertFalse(session.view()["can_save"])
        session.undo()
        self.assertEqual("Esstisch", session.room.query(Label.TABLE)[0].name)

    def test_tracking_loss_does_not_collect_and_cancel_preserves_saved_room(self):
        session = ScanSession(provider=Provider(), previous=room(elements=(table(),)))
        session.start()
        session.ingest(frame())
        previous = session.reconstruction.mesh()
        with self.assertRaises(RoomError):
            session.ingest(frame(2, tracking_valid=False))
        self.assertEqual(Stage.PAUSED, session.stage)
        self.assertEqual(previous, session.reconstruction.mesh())
        self.assertFalse(session.view()["world_visuals_allowed"])
        self.assertIsNone(session.view()["mesh"])
        self.assertFalse(session.view()["can_review"])
        with self.assertRaises(RoomError):
            session.review()
        with self.assertRaises(RoomError):
            session.resume(session.binding, tracking_valid=False)
        self.assertFalse(session.resume(replace(session.binding, origin_id=OTHER), tracking_valid=True))
        self.assertEqual(Stage.ALIGNMENT_REQUIRED, session.stage)
        session.cancel()
        self.assertEqual("Esstisch", session.previous.query(Label.TABLE)[0].name)


class EditorTests(unittest.TestCase):
    def test_surface_basis_roundtrips_for_rotated_and_reversed_corners(self):
        for label in (Label.TABLE, Label.WALL_FACE, Label.CEILING):
            for angle in (0., .5, 1.6, 2.9, 3.14, -1.6):
                x = (math.cos(angle), 0., math.sin(angle))
                y = (math.sin(angle), 0., -math.cos(angle)) if label != Label.WALL_FACE else (0., 1., 0.)
                for reverse in (1, -1):
                    draft = SurfaceDraft(label, ORIGIN)
                    p = (1., 1., -2.)
                    b = tuple(p[i]+2*x[i] for i in range(3))
                    c = tuple(b[i]+reverse*y[i] for i in range(3))
                    for n, point in enumerate((p, b, c), 1):
                        draft.add_corner(point, origin_id=ORIGIN, sample_ns=n, tracking_valid=True)
                    e = draft.finish("Fläche")
                    world = [e.pose.transform((*corner, 0.)) for corner in e.geometry.boundary]
                    for corner in (p, b, c):
                        self.assertTrue(any(sum((a-b)**2 for a, b in zip(corner, w)) < 1e-10 for w in world))

    def test_controller_corners_create_measured_table_and_volume(self):
        draft = SurfaceDraft(Label.TABLE, ORIGIN, element_id=TABLE)
        for sample_ns, point in enumerate(((0., .8, 0.), (2., .8, 0.), (2., .8, -1.)), 1):
            draft.add_corner(point, origin_id=ORIGIN, sample_ns=sample_ns, tracking_valid=True)
        element = draft.finish("Esstisch", furniture_depth_m=.8)
        self.assertFalse(element.confirmed)
        self.assertEqual(TABLE, element.id)
        self.assertEqual((1., .8, -.5), element.pose.position)
        self.assertAlmostEqual(0., element.pose.transform(element.geometry.box_center)[1]-.4)
        self.assertEqual((2., 1., .8), element.geometry.box_size)
        self.assertAlmostEqual(1., element.pose.rotate((0., 0., 1.))[1])
        draft.undo_corner()
        with self.assertRaises(RoomError):
            draft.finish("Unvollständig")

    def test_window_definition_uses_wall_and_rejects_wrong_origin(self):
        draft = SurfaceDraft(Label.WINDOW_FRAME, ORIGIN, parent_wall=WALL, element_id=WINDOW)
        with self.assertRaises(RoomError):
            draft.add_corner((0., 0., 0.), origin_id=OTHER, sample_ns=1, tracking_valid=True)
        self.assertFalse(draft.points)
        for sample_ns, point in enumerate(((-.5, -.5, 0.), (.5, -.5, 0.), (.5, .5, 0.)), 1):
            draft.add_corner(point, origin_id=ORIGIN, sample_ns=sample_ns, tracking_valid=True)
        window = replace(draft.finish("Fenster"), confirmed=True)
        self.assertEqual((window,), room(elements=(wall(), window)).query(Label.WINDOW_FRAME))

    def test_snapping_uses_reliable_measured_triangles_only(self):
        mesh = Mesh(((-1., -1., -1.), (1., -1., -1.), (0., 1., -1.)), ((0, 1, 2),), ("reliable",)*3)
        self.assertEqual((1., (0., 0., -1.)), pick_surface(mesh, (0., 0., 0.), (0., 0., -1.)))
        self.assertIsNone(pick_surface(mesh, (3., 0., 0.), (0., 0., -1.)))
        self.assertIsNone(pick_surface(replace(mesh, quality=("building",)*3), (0., 0., 0.), (0., 0., -1.)))
        with self.assertRaises(RoomError):
            pick_surface(mesh, (0., 0., 0.), (0., 0., -2.))


class PersistenceTests(unittest.TestCase):
    def test_revisions_conflicting_editors_drafts_and_test_data(self):
        with tempfile.TemporaryDirectory() as path:
            store = RoomStore(path)
            first = store.save(room(elements=(table(),)), expected_revision=0)
            draft = store.save_draft(SESSION, replace(first, name="Entwurf"))
            self.assertTrue((Path(path)/draft).is_file())
            self.assertEqual("Wohnzimmer", store.load(ROOM).name)
            second = store.save(replace(first, name="Gespeichert"), expected_revision=1)
            self.assertEqual(2, second.revision)
            self.assertEqual(first, store.load(ROOM, revision=1))
            with self.assertRaises(RoomError):
                store.save(replace(first, name="Veralteter Editor"), expected_revision=1)
            with self.assertRaises(RoomError):
                store.query(ROOM, Label.TABLE, current_origin_id=ORIGIN)
            self.assertEqual(second, store.load(ROOM))

    def test_interrupted_publish_keeps_previous_revision(self):
        with tempfile.TemporaryDirectory() as path:
            store = RoomStore(path)
            first = store.save(room(), expected_revision=0)
            with patch("room_scan.store.os.replace", side_effect=OSError("interrupted")):
                with self.assertRaises(OSError):
                    store.save(replace(first, name="Unveröffentlicht"), expected_revision=1)
            self.assertEqual(first, store.load(ROOM))
            self.assertTrue((Path(path)/f"{ROOM}.2.json").exists())
            self.assertFalse(list(Path(path).glob("*.lock")))

    def test_malformed_duplicate_and_foreign_files(self):
        for raw in (b'{"a":1,"a":2}', b'{"a":NaN}', b'\xff', b'{'):
            with self.assertRaises(RoomError):
                decode(raw)
        with tempfile.TemporaryDirectory() as path:
            store = RoomStore(path)
            store.save(room(), expected_revision=0)
            (Path(path)/f"{ROOM}.head").write_text('{"revision":true}')
            with self.assertRaises(RoomError):
                store.load(ROOM)
        if os.name == "posix":
            with tempfile.TemporaryDirectory() as path:
                root = Path(path)/"rooms"
                root.symlink_to(path, target_is_directory=True)
                with self.assertRaises(RoomError):
                    RoomStore(root)


if __name__ == "__main__":
    unittest.main()
