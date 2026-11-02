"""Exercise the actual private C++ module with independently specified geometry."""
from dataclasses import replace
import math
from pathlib import Path
import random
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from room_scan.capture import Binding, DepthFrame
from room_scan.fusion import NativeTsdfReconstruction
from room_scan.geometry import Pose, RoomError

LIBRARY = None
BINDING = Binding("11111111-1111-4111-a111-111111111111", "22222222-2222-4222-a222-222222222222",
                  "33333333-3333-4333-a333-333333333333")


def wall(sequence, *, x=0., noise=0., hole=False, confidence=1., surface_z=-2.):
    rng = random.Random(7183+sequence)
    yaw = x*.4
    pose = Pose((x, 1.6, .1), (0., math.sin(yaw/2), 0., math.cos(yaw/2)))
    depth, certainty = [], []
    for v in range(120):
        for u in range(160):
            ray = pose.rotate(((u-79.75)/110., -(v-59.75)/110., -1.))
            z = (surface_z-pose.position[2])/ray[2]
            point = tuple(p+d*z for p, d in zip(pose.position, ray))
            missing = hole and ((abs(point[0]) < .2 and 1. < point[1] < 1.4) or rng.random() < .15)
            depth.append(0. if missing else z+rng.gauss(0., noise))
            certainty.append(0. if missing else confidence)
    return DepthFrame(BINDING, sequence, sequence*1000000, pose, 160, 120, 110., 110., 79.75, 59.75,
                      tuple(depth), tuple(certainty))


class FusionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if LIBRARY is None:
            raise unittest.SkipTest("An explicit built native vision module is required")

    def create(self, **kwargs):
        result = NativeTsdfReconstruction(BINDING, library=LIBRARY, **kwargs)
        self.addCleanup(result.close)
        return result

    def test_noisy_known_wall_stays_in_world_and_preserves_unobserved_hole(self):
        fusion = self.create()
        for sequence in range(1, 9):
            fusion.integrate(wall(sequence, x=(sequence-4.5)*.025, noise=.015, hole=True))
        mesh = fusion.mesh()
        self.assertGreater(len(mesh.triangles), 3000)
        rmse = math.sqrt(sum((p[2]+2)**2 for p in mesh.vertices)/len(mesh.vertices))
        self.assertLess(rmse, .015)
        for face in mesh.triangles:
            center = tuple(sum(mesh.vertices[i][axis] for i in face)/3 for axis in range(3))
            self.assertFalse(abs(center[0]) < .1 and 1.1 < center[1] < 1.3)

    def test_quality_needs_distinct_views_and_strong_actual_measurements(self):
        repeated = self.create()
        observation = wall(1)
        for sequence in range(1, 5):
            repeated.integrate(replace(observation, sequence=sequence, exposure_ticks=sequence*1000000))
        self.assertEqual(0, repeated.status()["reliable_cells"])
        for sequence, x in ((5, -.08), (6, .08)):
            repeated.integrate(wall(sequence, x=x))
        self.assertGreater(repeated.status()["reliable_cells"], 0)
        weak = self.create()
        for sequence, x in ((1, -.08), (2, 0.), (3, .08)):
            weak.integrate(wall(sequence, x=x, confidence=.6))
        self.assertEqual(0, weak.status()["reliable_cells"])
        self.assertGreater(weak.status()["uncertain_cells"], 0)

    def test_failed_observation_keeps_accepted_volume_mesh_and_identity(self):
        fusion = self.create()
        observation = wall(1)
        fusion.integrate(observation)
        before = fusion.mesh(), fusion.status()
        blocks = fusion._library.sfnp_fusion_block_count(fusion._context)
        for bad in (observation, replace(observation, sequence=2),
                    replace(wall(2), tracking_valid=False),
                    replace(wall(2), binding=replace(BINDING, origin_id="44444444-4444-4444-a444-444444444444"))):
            with self.assertRaises(RoomError):
                fusion.integrate(bad)
            self.assertEqual(before, (fusion.mesh(), fusion.status()))
            self.assertEqual(blocks, fusion._library.sfnp_fusion_block_count(fusion._context))
        tiny = self.create(max_vertices=1)
        with self.assertRaises(RoomError):
            tiny.integrate(wall(1))
        self.assertEqual(0, tiny.sequence)
        self.assertEqual(0, tiny.accepted_frames)
        self.assertFalse(tiny.mesh().vertices)
        self.assertEqual(0, tiny._library.sfnp_fusion_block_count(tiny._context))

    def test_revisiting_a_heavily_scanned_surface_corrects_geometry_and_quality(self):
        fusion = self.create()
        for sequence in range(1, 81):
            fusion.integrate(wall(sequence, x=(sequence % 3-1)*.08,
                                  surface_z=-2.04, confidence=.6))
        self.assertEqual(0, fusion.status()["reliable_cells"])
        for sequence in range(81, 145):
            fusion.integrate(wall(sequence, x=(sequence % 3-1)*.08))
        mesh = fusion.mesh()
        central = [(point, quality) for point, quality in zip(mesh.vertices, mesh.quality)
                   if abs(point[0]) < .4 and 1.3 < point[1] < 1.9]
        self.assertGreater(len(central), 20)
        self.assertLess(max(abs(point[2]+2.) for point, _ in central), .005)
        self.assertGreater(sum(quality == "reliable" for _, quality in central), len(central)//2)

    def test_mesh_faces_point_towards_observed_side_and_close_retains_snapshot(self):
        fusion = self.create()
        fusion.integrate(wall(1))
        before = fusion.mesh()
        for face in before.triangles:
            a, b, d = (before.vertices[i] for i in face)
            self.assertGreater((b[0]-a[0])*(d[1]-a[1])-(b[1]-a[1])*(d[0]-a[0]), 0.)
        fusion.close()
        self.assertEqual(before, fusion.mesh())
        fusion.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Usage: test_room_fusion_native.py <owned-native-module>")
    LIBRARY = Path(sys.argv.pop(1)).absolute()
    unittest.main()
