#!/usr/bin/env python3
"""Prepare and execute the private Windows Gaussian runtime without a Frame."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--cpu-development', action='store_true',
                        help='Verify the actual CUDA wheel on CPU; does not grant GPU scanner readiness')
    args = parser.parse_args()
    if sys.platform != 'win32' or sys.version_info[:2] != (3, 14):
        raise RuntimeError('This check requires Windows Python 3.14')
    root = Path(__file__).resolve().parents[1]
    specification = importlib.util.spec_from_file_location('surface_runtime_setup',
                                                          root/'setup/steam_link_setup_room.py')
    helper = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(helper)
    manifest = root/'packaging/room-surface-runtime.json'
    runtime = helper.prepare_surface_runtime(manifest, args.cache)
    if 'torch' in sys.modules or 'onnxruntime' in sys.modules:
        raise RuntimeError('Preparation imported a numerical runtime into its parent')
    sys.path.insert(0, str(root))
    from room_reconstruction.surface_runtime import warmup_command
    command, environment = warmup_command(runtime, manifest, cpu_development=args.cpu_development)
    result = subprocess.run(command, env=environment, cwd=runtime, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError('Private Gaussian child failed:\n'+result.stderr[-12000:])
    report = json.loads(result.stdout)
    if (report.get('schema') != 'sfnp-room-surface-warmup-v1' or report.get('result') != 'PASS'
            or report.get('gpu_ready') != (not args.cpu_development)):
        raise RuntimeError('Private Gaussian child returned an invalid readiness result')
    if 'torch' in sys.modules or 'onnxruntime' in sys.modules:
        raise RuntimeError('Private Gaussian child contaminated its parent')
    report['parent_runtime_imported'] = False
    print(json.dumps(report, allow_nan=False))


if __name__ == '__main__':
    main()
