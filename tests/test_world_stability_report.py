import copy
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from analyze_world_stability import analyze, quantiles

BINARY = None
if __name__ == "__main__" and len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    BINARY = sys.argv.pop(1)


class ReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY:
            raise unittest.SkipTest("Pass the built world-stability-test binary for the real emitter fixture")
        cls.transcript = subprocess.check_output([BINARY, "--transcript"], text=True)
        cls.records = [json.loads(line) for line in cls.transcript.splitlines()]

    def encoded(self, records):
        return "\n".join(json.dumps(r) for r in records)

    def test_actual_cpp_emitter_and_phase_summary(self):
        report = analyze(self.transcript, "reference-stable-scene-wobbles")
        self.assertEqual(report["samples"], 3)
        self.assertTrue(report["clean_stop"] and report["requested_stop"])
        self.assertEqual(set(report["phases"]), {"still", "rotation", "translation"})
        self.assertEqual(report["phases"]["rotation"]["observer_read_span_us"]["p95"], 1)
        self.assertIsNone(report["motion_to_photon_ms"])
        self.assertFalse(report["exact_stream_pose_proven"] or report["world_lock_error_measured"] or report["causal_diagnosis_proven"])

    def test_visibility_control_is_not_a_world_reference(self):
        transcript = subprocess.check_output([BINARY, "--transcript-visibility"], text=True)
        report = analyze(transcript, "visibility-marker-seen")
        self.assertEqual(report["reference_mode"], "head-locked-visibility")
        self.assertFalse(report["world_reference"] or report["world_lock_error_measured"] or report["causal_diagnosis_proven"])
        self.assertTrue(report["clean_stop"])
        for observation in ("reference-stable-scene-wobbles", "both-wobble", "reference-unseen"):
            with self.subTest(observation=observation), self.assertRaises(ValueError):
                analyze(transcript, observation)
        with self.assertRaises(ValueError):
            analyze(self.transcript, "visibility-marker-seen")

    def test_legacy_world_recordings_remain_readable(self):
        records = copy.deepcopy(self.records)
        for key in ("reference_mode", "world_reference", "head_relative_transform", "dashboard_visibility_allowed", "sort_order"):
            records[0].pop(key, None)
        report = analyze(self.encoded(records), "reference-unseen")
        self.assertEqual(report["reference_mode"], "world-fixed")
        self.assertTrue(report["world_reference"])

    def test_wrong_mode_or_head_control_motion_claim_is_rejected(self):
        transcript = subprocess.check_output([BINARY, "--transcript-visibility"], text=True)
        for key, value in (("reference_mode", "unknown"), ("world_reference", True), ("standing_anchor", [0]*12)):
            records = [json.loads(line) for line in transcript.splitlines()]
            records[0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                analyze(self.encoded(records))
        records = [json.loads(line) for line in transcript.splitlines()]
        records[1]["phase"] = 1
        with self.assertRaises(ValueError):
            analyze(self.encoded(records))

    def test_dashboard_samples_are_reported_separately(self):
        records = copy.deepcopy(self.records)
        records[2]["dashboard"] = True
        report = analyze(self.encoded(records))
        self.assertEqual(report["phases"]["rotation"]["dashboard_samples"], 1)

    def test_partial_capture_does_not_claim_a_clean_end(self):
        report = analyze(self.encoded(self.records[:-1]))
        self.assertFalse(report["clean_stop"] or report["requested_stop"])

    def test_recenter_is_an_interruption_even_after_clean_overlay_removal(self):
        records = copy.deepcopy(self.records)
        records[-1]["reason"] = 1
        report = analyze(self.encoded(records))
        self.assertTrue(report["clean_stop"])
        self.assertFalse(report["requested_stop"])

    def test_invalid_numbers_and_sequence_are_rejected(self):
        for key, value in (("seq", 2), ("end_monotonic_us", 0), ("phase", 99), ("exact_stream_pose_proven", True)):
            records = copy.deepcopy(self.records)
            records[1][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                analyze(self.encoded(records))
        with self.assertRaises(ValueError):
            analyze(self.transcript.replace('"angular_rad_s":[0,0.5,0]', '"angular_rad_s":[0,NaN,0]'))

    def test_sessions_and_post_budget_data_cannot_be_joined(self):
        with self.assertRaises(ValueError):
            analyze(self.transcript + self.transcript)
        records = copy.deepcopy(self.records)
        records.insert(1, {"schema": records[0]["schema"], "event": "budget"})
        with self.assertRaises(ValueError):
            analyze(self.encoded(records))

    def test_invalid_timing_is_not_a_zero_latency_sample(self):
        records = copy.deepcopy(self.records)
        records[2]["frame_timing_valid"] = False
        records[2]["client_interval_ms"] = 0
        report = analyze(self.encoded(records))
        self.assertIsNone(report["phases"]["rotation"]["public_client_interval_ms"])

    def test_quantiles_do_not_invent_missing_values(self):
        self.assertIsNone(quantiles([]))
        self.assertEqual(quantiles([4, 1, 3, 2])["p95"], 4)


if __name__ == "__main__":
    unittest.main()
