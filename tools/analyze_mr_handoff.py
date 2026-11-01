#!/usr/bin/env python3
"""Bounded Frame journal counter windows; no frame joins or display-latency claims."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import time
import uuid

MAX_BYTES = 32 * 1024 * 1024
MAX_LINES = 100000
MARKER_SCHEMA = "sfnp-mr-handoff-marker-v1"
PREFIX = r"\[Steam Link Native Passthrough\] "
ROLE = r"(?P<role>video|compositor)"
NUMBER = r"([0-9]{1,20})(?![0-9])"
# Each family is evaluated independently: adjacent fprintf calls are not an
# atomic snapshot, and a journal timestamp does not identify a decoded picture.
FAMILIES = {
    "handoff": (r"handoff duration " + ROLE + r": samples=" + NUMBER + r", total-us=" + NUMBER + r", approximate-mean-us=" + NUMBER,
                ("samples", "total_us", "lifetime_mean_us"), ("samples", "total_us")),
    "handoff_thresholds": (r"paired handoff latency " + ROLE + r": max-us=" + NUMBER + r"; over-11ms/22ms/50ms=" + NUMBER + "/" + NUMBER + "/" + NUMBER,
                           ("max_us", "over11", "over22", "over50"), ("max_us", "over11", "over22", "over50")),
    "mask_stages": (r"HEVC mask stages (?P<role>compositor): started/collected=" + NUMBER + "/" + NUMBER + r"; queue total/max-us=" + NUMBER + "/" + NUMBER + r"; completed-to-collect total/max-us=" + NUMBER + "/" + NUMBER + r"; publication samples/total/max-us=" + NUMBER + "/" + NUMBER + "/" + NUMBER + r"; R8-buffer grows/reuses=" + NUMBER + "/" + NUMBER,
                    ("started", "collected", "queue_us", "queue_max_us", "collect_us", "collect_max_us", "publications", "publish_us", "publish_max_us", "grows", "reuses"),
                    ("started", "collected", "queue_us", "queue_max_us", "collect_us", "collect_max_us", "publications", "publish_us", "publish_max_us", "grows", "reuses")),
    "mask_worker": (r"HEVC mask pipeline " + ROLE + r": workers=" + NUMBER + r", slots=" + NUMBER + r"; submitted/backpressure/stale=" + NUMBER + "/" + NUMBER + "/" + NUMBER + r"; contexts=" + NUMBER + r"; attempts=" + NUMBER + r"; group-affine IPPP8; worker mean/max-us=" + NUMBER + "/" + NUMBER,
                    ("workers", "slots", "submitted", "backpressure", "stale", "contexts", "attempts", "lifetime_mean_us", "max_us"),
                    ("submitted", "backpressure", "stale", "contexts", "attempts", "max_us")),
}
PATTERNS = {name: re.compile(PREFIX + expression) for name, (expression, _, _) in FAMILIES.items()}
KNOWN_COUNTER = re.compile(PREFIX + r"(?:handoff duration|paired handoff latency|HEVC mask stages|HEVC mask pipeline) (?:video|compositor):")


def bounded_lines(text):
    if len(text.encode("utf-8")) > MAX_BYTES:
        raise ValueError("Input exceeds the byte budget")
    lines = text.splitlines()
    if len(lines) > MAX_LINES or any(len(line) > 131072 for line in lines):
        raise ValueError("Input exceeds the line budget")
    return [line for line in lines if line.strip()]


def integer(value):
    if isinstance(value, str) and re.fullmatch(r"[0-9]{1,20}", value):
        value = int(value)
    if type(value) is not int or not 0 <= value < 2**64:
        raise ValueError("Invalid unsigned counter or timestamp")
    return value


def boot_id(value):
    if type(value) is not str:
        raise ValueError("Missing boot identity")
    try:
        return uuid.UUID(value).hex
    except (ValueError, AttributeError):
        raise ValueError("Invalid boot identity") from None


def decode(line):
    def pairs(values):
        result = {}
        for key, value in values:
            if key in result:
                raise ValueError("Duplicate JSON field")
            result[key] = value
        return result
    def invalid(_):
        raise ValueError("Nonfinite JSON value")
    value = json.loads(line, object_pairs_hook=pairs, parse_constant=invalid)
    if type(value) is not dict:
        raise ValueError("Expected a JSON object")
    return value


def windows(text):
    records = [decode(line) for line in bounded_lines(text)]
    if len(records) > 64:
        raise ValueError("Too many operator markers")
    starts, result = {}, {}
    for record in records:
        if (record.get("schema") != MARKER_SCHEMA or record.get("phase") not in ("mr-on", "mr-off")
                or record.get("event") not in ("start", "end")
                or record.get("mode_confirmed_by_operator") is not True
                or type(record.get("game")) is not str or not 0 < len(record["game"]) <= 128):
            raise ValueError("Invalid operator mode marker")
        phase = record["phase"]
        current = {"boot_id": boot_id(record.get("boot_id")), "time": integer(record.get("monotonic_us")), "game": record["game"]}
        if record["event"] == "start":
            if phase in starts or phase in result:
                raise ValueError("Duplicate phase start")
            starts[phase] = current
        else:
            start = starts.pop(phase, None)
            if (not start or phase in result or start["boot_id"] != current["boot_id"]
                    or start["game"] != current["game"] or start["time"] >= current["time"]):
                raise ValueError("Phase boundary changed identity or time")
            result[phase] = {"boot_id": current["boot_id"], "game": current["game"], "start_us": start["time"], "end_us": current["time"]}
    if starts or not result:
        raise ValueError("Missing complete phase boundaries")
    ordered = sorted(result.values(), key=lambda x: x["start_us"])
    if len({(x["boot_id"], x["game"]) for x in ordered}) != 1:
        raise ValueError("Do not compare different games or device boots")
    if any(a["end_us"] > b["start_us"] for a, b in zip(ordered, ordered[1:])):
        raise ValueError("Operator phases overlap")
    return result


def snapshots(text):
    result, ignored = [], 0
    for line in bounded_lines(text):
        record = decode(line)
        message = record.get("MESSAGE", "")
        if type(message) is not str:
            ignored += 1
            continue
        found = None
        for family, pattern in PATTERNS.items():
            match = pattern.search(message)
            if match:
                found = (family, match)
                break
        if not found:
            if KNOWN_COUNTER.search(message):
                raise ValueError("Unrecognized or out-of-bounds counter format")
            ignored += 1
            continue
        family, match = found
        pid = integer(record.get("_PID"))
        if not 0 < pid < 2**31:
            raise ValueError("Missing journal process identity")
        stream = record.get("_STREAM_ID", "")
        if type(stream) is not str or len(stream) > 128 or not stream.isascii():
            raise ValueError("Invalid journal stream identity")
        metadata = {}
        for field in ("_COMM", "_SYSTEMD_USER_UNIT"):
            value = record.get(field, "")
            if type(value) is not str or len(value) > 256 or any(ord(c) < 32 for c in value):
                raise ValueError("Invalid journal source metadata")
            metadata[field] = value
        names = FAMILIES[family][1]
        values = dict(zip(names, (integer(v) for v in match.groups()[1:])))
        result.append({"boot_id": boot_id(record.get("_BOOT_ID")), "pid": pid, "stream": stream,
                       "role": match["role"], "family": family, "time": integer(record.get("__MONOTONIC_TIMESTAMP")),
                       "comm": metadata["_COMM"], "unit": metadata["_SYSTEMD_USER_UNIT"], "values": values})
    return result, ignored


def mean_ms(total, count):
    return total / count / 1000 if count > 0 else None


def worker_mean_bounds(a, b):
    """Bound an interval mean from the emitter's floor(total_us / attempts).

    Never multiply a rounded mean into an exact total. The worker statistics
    come from one locked Pool::statistics snapshot; unlike adjacent log families
    the printed attempts and mean describe that same snapshot.
    """
    count = b["attempts"] - a["attempts"]
    if count <= 0:
        return None
    def totals(value):
        n, mean = value["attempts"], value["lifetime_mean_us"]
        if n == 0:
            if mean != 0:
                raise ValueError("Nonzero worker mean without attempts")
            return 0, 0
        return n * mean, min(2**64 - 1, n * (mean + 1) - 1)
    lo_a, hi_a = totals(a)
    lo_b, hi_b = totals(b)
    lower, upper = max(0, lo_b - hi_a), hi_b - lo_a
    if upper < lower:
        raise ValueError("Worker mean and attempts contradict cumulative duration")
    return {"lower_ms": lower / count / 1000, "upper_ms": upper / count / 1000,
            "basis": "Bounds from integer-truncated cumulative means; not an exact interval mean"}


def epoch_report(records, family):
    first, last = records[0], records[-1]
    a, b = first["values"], last["values"]
    difference = {name: b[name] - a[name] for name in FAMILIES[family][2]}
    metrics = None
    if len(records) >= 2:
        d = difference
        if family == "handoff":
            metrics = {"callbacks": d["samples"], "total_callback_ms": d["total_us"] / 1000,
                       "approximate_mean_callback_ms": mean_ms(d["total_us"], d["samples"])}
        elif family == "handoff_thresholds":
            metrics = {"callbacks_over_11ms": d["over11"], "callbacks_over_22ms": d["over22"],
                       "callbacks_over_50ms": d["over50"], "lifetime_max_ms_at_end": b["max_us"] / 1000}
        elif family == "mask_stages":
            metrics = {"started": d["started"], "collected": d["collected"], "publications": d["publications"],
                       "approximate_queue_mean_ms": mean_ms(d["queue_us"], d["started"]),
                       "approximate_completed_to_collect_mean_ms": mean_ms(d["collect_us"], d["collected"]),
                       "approximate_publication_mean_ms": mean_ms(d["publish_us"], d["publications"]),
                       "queue_lifetime_max_ms_at_end": b["queue_max_us"] / 1000,
                       "collect_lifetime_max_ms_at_end": b["collect_max_us"] / 1000,
                       "publication_lifetime_max_ms_at_end": b["publish_max_us"] / 1000,
                       "buffer_grows": d["grows"], "buffer_reuses": d["reuses"]}
        else:
            metrics = {name: d[name] for name in ("submitted", "backpressure", "stale", "contexts", "attempts")}
            metrics.update(worker_lifetime_approximate_mean_ms_at_end=b["lifetime_mean_us"] / 1000,
                           worker_lifetime_max_ms_at_end=b["max_us"] / 1000,
                           worker_interval_mean_bounds=worker_mean_bounds(a, b))
    return {"first_report_us": first["time"], "last_report_us": last["time"], "reports": len(records),
            "status": "counter-deltas" if metrics is not None else "insufficient-snapshots", "metrics": metrics}


def analyze(journal, markers):
    phases = windows(markers)
    reports, ignored = snapshots(journal)
    output = {}
    for phase, boundary in phases.items():
        groups = {}
        for report in reports:
            if report["boot_id"] != boundary["boot_id"] or not boundary["start_us"] <= report["time"] <= boundary["end_us"]:
                continue
            key = (report["pid"], report["stream"], report["role"], report["family"], report["comm"], report["unit"])
            groups.setdefault(key, []).append(report)
        entries = []
        for (pid, stream, role, family, comm, unit), selected in sorted(groups.items()):
            ordered, epochs = sorted(selected, key=lambda x: x["time"]), []
            for record in ordered:
                if epochs and record["time"] == epochs[-1][-1]["time"]:
                    if record["values"] != epochs[-1][-1]["values"]:
                        raise ValueError("Conflicting snapshots at the same process timestamp")
                    continue
                if not epochs or any(record["values"][k] < epochs[-1][-1]["values"][k] for k in FAMILIES[family][2]):
                    epochs.append([])
                epochs[-1].append(record)
            entries.append({"journal_pid": pid, "journal_stream": stream, "role": role, "family": family,
                            "journal_comm": comm, "journal_unit": unit,
                            "journal_source_kind": "log-relay" if unit == "steamvr-logs.service" else "unspecified",
                            "counter_resets_observed": len(epochs) - 1,
                            "epochs": [epoch_report(epoch, family) for epoch in epochs]})
        output[phase] = {**boundary, "status": "observed" if entries else "no-counter-snapshots", "groups": entries}
    return {"schema": "sfnp-mr-handoff-profile-v1", "phases": output, "ignored_journal_records": ignored,
            "both_operator_windows_present": set(phases) == {"mr-on", "mr-off"},
            "motion_to_photon_ms": None, "exact_stream_pose_proven": False, "causal_diagnosis_proven": False,
            "scope": "Independent journal source/role counter deltas within Frame-clock operator windows. Modes are operator assertions. A journal PID may identify a log relay, not the native process; relay clocks are forwarding times with unmeasured delay. Counter resets are detected, not all native restarts. Reports are not atomic; maxima and printed worker means are lifetime values; interval worker means are bounded from rounding only. Do not add stage means or treat journal time as image/pose identity."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    mark = sub.add_parser("mark")
    mark.add_argument("--phase", required=True, choices=("mr-on", "mr-off"))
    mark.add_argument("--event", required=True, choices=("start", "end"))
    mark.add_argument("--game", required=True)
    mark.add_argument("--mode-confirmed", action="store_true", required=True)
    read = sub.add_parser("analyze")
    read.add_argument("--journal", type=Path, required=True)
    read.add_argument("--markers", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "mark":
            if not 0 < len(args.game) <= 128:
                raise ValueError("Invalid game label")
            result = {"schema": MARKER_SCHEMA, "phase": args.phase, "event": args.event, "game": args.game,
                      "mode_confirmed_by_operator": True, "boot_id": boot_id(Path("/proc/sys/kernel/random/boot_id").read_text().strip()),
                      "monotonic_us": time.monotonic_ns() // 1000}
        else:
            if args.journal.stat().st_size > MAX_BYTES or args.markers.stat().st_size > 128 * 1024:
                raise ValueError("Input exceeds the byte budget")
            result = analyze(args.journal.read_text(encoding="utf-8"), args.markers.read_text(encoding="utf-8"))
        print(json.dumps(result, indent=2 if args.command == "analyze" else None, allow_nan=False))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
