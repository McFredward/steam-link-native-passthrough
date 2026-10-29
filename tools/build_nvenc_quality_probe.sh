#!/usr/bin/env bash
# Cross-build only; never install or run against a device.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo 'Usage: build_nvenc_quality_probe.sh SDK_DIRECTORY OUTPUT_DIRECTORY' >&2; exit 2; fi
probe_sdk=$(realpath -e -- "$1")
probe_output=$(realpath -m -- "$2")
probe_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 - "$probe_sdk" <<'PY'
from pathlib import Path
import sys,hashlib
assert hashlib.sha256((Path(sys.argv[1])/'nvEncodeAPI.h').read_bytes()).hexdigest()=='be0c29a41ae3260fd016a9001fd959e747bac93ad21411759736abba4ffdea86'
PY
mkdir -p -- "$probe_output"
# The embedded revision must describe the exact input code being delivered.
git -C "$probe_root" diff --quiet HEAD -- tests/mask-hevc/nvenc_quality_probe.cpp tests/mask-hevc/probe_pattern.h pc/stream-bridge-mask-hevc.cpp pc/stream-bridge-mask-hevc.h pc/stream-bridge-mask-hevc-rate.h pc/stream-bridge-mask-hevc-platform.h tools/build_nvenc_quality_probe.sh
probe_revision=$(git -C "$probe_root" rev-parse HEAD)
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I"$probe_root" -isystem "$probe_sdk" \
  -DSFNP_PROBE_SOURCE_REVISION=\""$probe_revision"\" \
  "$probe_root/tests/mask-hevc/nvenc_quality_probe.cpp" "$probe_root/pc/stream-bridge-mask-hevc.cpp" \
  -municode -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -Wl,--no-insert-timestamp \
  -ld3d11 -ldxgi -luuid -lwinmm -o "$probe_output/sfnp-nvenc-quality-probe.exe"
sha256sum -- "$probe_output/sfnp-nvenc-quality-probe.exe"
