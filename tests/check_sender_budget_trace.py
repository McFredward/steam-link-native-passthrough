#!/usr/bin/env python3
"""Replay observed budget samples through production policy; never simulate native Auto as fact."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

SAMPLE = re.compile(
    r"producer wire budget: .*last video requested/passed=(\d+)/(\d+) bit/s; "
    r"reserved metadata peak/mean=(\d+)/(\d+) bit/s; "
    r"VBV requested/passed=(\d+)/(\d+) bits; delay=(\d+)/(\d+) bits")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    digest = hashlib.sha256()
    rows = []
    with args.log.open("rb") as source_log:
        for number, raw_line in enumerate(source_log, 1):
            digest.update(raw_line)
            match = SAMPLE.search(raw_line.decode("utf-8", errors="replace"))
            if match:
                rows.append(dict(zip(("requested", "video", "peak", "mean", "requested_vbv", "video_vbv",
                                      "requested_delay", "video_delay"), map(int, match.groups())), line=number))
    if not rows:
        raise RuntimeError("No complete sender wire-budget samples found")
    # This driver links the real policy header. The private trace, account IDs,
    # source PIDs, addresses and unrelated log records are never copied out.
    source = r'''
#include "pc/stream-bridge-wire-budget.h"
#include <iostream>
using stream_bridge::sender::WireBudget;
int main() {
    unsigned requested,peak,mean;
    while(std::cin>>requested>>peak>>mean) {
        std::cout<<WireBudget::videoRate(requested,peak)<<' '
                 <<WireBudget::videoRate(requested,mean)<<' '
                 <<WireBudget::videoRate(300000000,peak)<<' '
                 <<WireBudget::videoRate(347500000,peak)<<'\n';
    }
}
'''
    with tempfile.TemporaryDirectory(prefix="sfnp-budget-replay-") as temporary:
        temporary = Path(temporary)
        implementation, binary = temporary / "replay.cpp", temporary / "replay"
        implementation.write_text(source)
        subprocess.run([args.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                        "-I" + str(root), str(implementation), "-o", str(binary)], check=True, timeout=60)
        data = "".join(f'{r["requested"]} {r["peak"]} {r["mean"]}\n' for r in rows)
        completed = subprocess.run([str(binary)], input=data, text=True, stdout=subprocess.PIPE,
                                   check=True, timeout=10)
    calculated = [list(map(int, line.split())) for line in completed.stdout.splitlines()]
    if len(calculated) != len(rows):
        raise RuntimeError("Replay produced a different sample count")
    for row, (actual, mean, fixed300, fixed347) in zip(rows, calculated):
        if not row["requested"]:
            raise RuntimeError(f'Zero native requested rate at line {row["line"]}')
        if actual != row["video"]:
            raise RuntimeError(f'Production rate differs from observed sample at line {row["line"]}')
        if row["requested_vbv"] and row["video_vbv"] > max(1, row["requested_vbv"] * actual // row["requested"]):
            raise RuntimeError(f'Observed VBV exceeds scaled caller buffer at line {row["line"]}')
        if row["video_delay"] > row["video_vbv"]:
            raise RuntimeError(f'Observed delay exceeds forwarded buffer at line {row["line"]}')
        row.update(mean_only_counterfactual=mean, fixed_300mbps_counterfactual=fixed300,
                   fixed_347_5mbps_counterfactual=fixed347)
    # Counterfactuals use the same observed reserve samples. A different native
    # setting/controller would change output, network behavior and future samples;
    # these calculations do not predict a headset result or prove link capacity.
    substantial = [r for r in rows if r["peak"] >= 1000000]
    summary = {
        "schema": "sfnp-wire-budget-trace-replay-v1",
        "source_log_sha256": digest.hexdigest(),
        "policy_sha256": hashlib.sha256((root / "pc/stream-bridge-wire-budget.h").read_bytes()).hexdigest(),
        "observed_samples": len(rows), "all_observed_video_rates_match_single_reservation": True,
        "observed_vbv_scaled_upper_bound_and_delay_checks_pass": True,
        "metadata_reserve_at_least_1mbps_samples": len(substantial),
        "video_floor_samples_with_reserve_at_least_1mbps": sum(r["video"] <= 1000000 for r in substantial),
        "mean_only_floor_samples_counterfactual": sum(r["mean_only_counterfactual"] <= 1000000 for r in substantial),
        "mean_metadata_alone_exceeds_requested_samples": sum(r["mean"] >= r["requested"] for r in substantial),
        "fixed_300mbps_minimum_video_counterfactual": min((r["fixed_300mbps_counterfactual"] for r in substantial), default=None),
        "fixed_347_5mbps_minimum_video_counterfactual": min((r["fixed_347_5mbps_counterfactual"] for r in substantial), default=None),
        "samples": rows,
        "scope": ["Five-second snapshots are not a complete per-frame trace or a duration estimate.",
                  "Frame rate is absent from this log; VBV validation checks only its independent upper bounds.",
                  "No Valve Auto internals, physical capacity or changed-setting future samples are simulated.",
                  "The 1Mbit/s metadata threshold is only a reporting filter, never source/session permission.",
                  "Mean-only reservation is a diagnostic control, not a proposed production policy."],
        "device_access": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(f'{len(rows)} observed rates match one production metadata reservation; '
          f'{summary["video_floor_samples_with_reserve_at_least_1mbps"]}/{len(substantial)} snapshots with metadata >=1Mbit/s hit the floor; '
          f'{summary["mean_metadata_alone_exceeds_requested_samples"]} have mean metadata above the caller budget.')


if __name__ == "__main__":
    main()
