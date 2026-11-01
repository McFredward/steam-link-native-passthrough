#!/usr/bin/env python3
"""Summarize local public timing context; never infer a video pose or world error."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path

SCHEMA = "sfnp-frame-world-stability-v1"
MAX_BYTES = 8 * 1024 * 1024
PHASES = {0: "still", 1: "rotation", 2: "translation", 3: "comparison"}
OBSERVATIONS = ("not-recorded", "reference-stable-scene-wobbles", "both-wobble", "reference-unseen", "unclear", "visibility-marker-seen", "visibility-marker-unseen")


def finite(value):
    return type(value) in (int, float) and math.isfinite(value) and abs(value) < 1e12


def quantiles(values):
    values = sorted(values)
    if not values:
        return None
    def percentile(fraction):
        return values[max(0, math.ceil(len(values) * fraction) - 1)]
    return {"count": len(values), "p50": percentile(.5), "p95": percentile(.95), "p99": percentile(.99), "max": values[-1]}


def analyze(text, observed="not-recorded"):
    if observed not in OBSERVATIONS:
        raise ValueError("Unknown explicit visual observation")
    if len(text.encode("utf-8")) > MAX_BYTES:
        raise ValueError("Recording exceeds its byte budget")
    ready = stopped = refused = None
    samples, ignored, last_time, budget = [], 0, 0, False
    def no_constant(_):
        raise ValueError("Nonfinite JSON number")
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            ignored += 1
            continue
        record = json.loads(line, parse_constant=no_constant)
        if not isinstance(record, dict) or record.get("schema") != SCHEMA:
            raise ValueError("Not a Frame world-reference record")
        event = record.get("event")
        if event == "ready":
            if ready or refused or stopped:
                raise ValueError("Do not combine different reference sessions")
            if record.get("frame_local") is not True or record.get("headset_visibility_verified") is not False:
                raise ValueError("Unexpected reference authority")
            ready = record
        elif event == "sample":
            if not ready or stopped or refused or budget or len(samples) >= 6000:
                raise ValueError("Sample outside the recorded session/budget")
            if type(record.get("seq")) is not int or record["seq"] != len(samples) + 1 or type(record.get("phase")) is not int or record["phase"] not in PHASES:
                raise ValueError("Invalid sample sequence/phase")
            begin, end = record.get("begin_monotonic_us"), record.get("end_monotonic_us")
            if type(begin) is not int or type(end) is not int or begin < last_time or end < begin:
                raise ValueError("Invalid local monotonic read span")
            if record.get("exact_stream_pose_proven") is not False:
                raise ValueError("Public compositor context does not prove a stream pose")
            for key, length in (("head_standing", 12), ("angular_rad_s", 3), ("public_frame_pose", 12)):
                value = record.get(key)
                if not isinstance(value, list) or len(value) != length or not all(finite(x) for x in value):
                    raise ValueError("Invalid pose/vector shape")
            last_time = end
            samples.append(record)
        elif event == "budget":
            if not ready or stopped or budget:
                raise ValueError("Invalid logging-budget boundary")
            budget = True
        elif event == "stopped":
            if (not ready or stopped or refused or type(record.get("samples")) is not int or record["samples"] != len(samples)
                    or type(record.get("clean")) is not bool or type(record.get("reason")) is not int or not 0 <= record["reason"] <= 5):
                raise ValueError("Invalid stop receipt")
            stopped = record
        elif event == "refused":
            if ready or samples or refused:
                raise ValueError("Invalid startup refusal")
            refused = record
        else:
            raise ValueError("Unknown reference event")
    if not ready and not refused:
        raise ValueError("No reference session found")
    mode=ready.get("reference_mode", "world-fixed") if ready else None
    if ready:
        if mode not in ("world-fixed", "head-locked-visibility"):
            raise ValueError("Unknown reference mode")
        if ready.get("world_reference", True) is not (mode=="world-fixed"):
            raise ValueError("Reference mode and world authority disagree")
        if mode=="head-locked-visibility" and ready.get("standing_anchor") is not None:
            raise ValueError("A head-locked control cannot have a standing anchor")
        if mode=="head-locked-visibility" and any(s["phase"]!=0 for s in samples):
            raise ValueError("A head-locked control cannot record world-motion phases")
        if mode=="head-locked-visibility" and observed in ("reference-stable-scene-wobbles", "both-wobble", "reference-unseen"):
            raise ValueError("A head-locked visibility control cannot establish world stability")
        if mode=="world-fixed" and observed.startswith("visibility-marker-"):
            raise ValueError("Visibility-control observations require a visibility-control recording")
    phases = {}
    for phase, name in PHASES.items():
        selected = [s for s in samples if s["phase"] == phase]
        if not selected:
            continue
        valid_head = [s for s in selected if s.get("head_valid") is True and s.get("numeric_valid") is True]
        valid_frame = [s for s in selected if s.get("frame_timing_valid") is True and s.get("numeric_valid") is True]
        intervals = [s["client_interval_ms"] for s in valid_frame if finite(s.get("client_interval_ms")) and s["client_interval_ms"] >= 0]
        angular = [math.degrees(math.sqrt(sum(x*x for x in s["angular_rad_s"]))) for s in valid_head]
        phases[name] = {
            "samples": len(selected), "valid_head_samples": len(valid_head), "valid_public_timing_samples": len(valid_frame),
            "observer_read_span_us": quantiles([s["end_monotonic_us"]-s["begin_monotonic_us"] for s in selected]),
            "head_angular_speed_degrees_s": quantiles(angular),
            "head_position_extent_m": [max(s["head_standing"][i] for s in valid_head)-min(s["head_standing"][i] for s in valid_head)
                                       for i in (3, 7, 11)] if valid_head else None,
            "public_client_interval_ms": quantiles(intervals),
            "public_frame_index_repeats": sum(a.get("frame_index")==b.get("frame_index") for a,b in zip(valid_frame,valid_frame[1:])),
            "marker_visible_api_samples": sum(s.get("marker_visible_api") is True for s in selected),
            "dashboard_samples": sum(s.get("dashboard") is True for s in selected),
        }
    return {
        "schema": "sfnp-frame-world-stability-summary-v1", "samples": len(samples), "ignored_non_json_lines": ignored,
        "local_api_ready": ready is not None, "startup_refusal": refused,
        "reference_mode": mode, "world_reference": bool(ready and mode=="world-fixed"),
        "clean_stop": bool(stopped and stopped.get("clean") is True),
        "requested_stop": bool(stopped and stopped.get("reason") == 0), "logging_budget_reached": budget,
        "explicit_user_visual_observation": observed, "phases": phases,
        "exact_stream_pose_proven": False, "motion_to_photon_ms": None, "world_lock_error_measured": False,
        "causal_diagnosis_proven": False,
        "scope": "Frame-local public tracking/compositor context; phase labels do not prove game/MR state; no PC/Frame clock subtraction",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path)
    parser.add_argument("--observed", choices=OBSERVATIONS, default="not-recorded")
    args = parser.parse_args()
    if args.recording.stat().st_size > MAX_BYTES:
        parser.error("Recording exceeds its byte budget")
    try:
        result = analyze(args.recording.read_text(encoding="utf-8"), args.observed)
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
