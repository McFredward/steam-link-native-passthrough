#!/usr/bin/env bash
# Offline compile-only preflight. Actual Windows/WARP jobs remain mandatory.
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo 'Usage: check_sender_windows_syntax.sh REVIEWED_PUBLIC_SDK_DIRECTORY' >&2
  exit 2
fi
sender_syntax_sdk=$(realpath -e -- "$1")
sender_syntax_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sender_syntax_cxx=${SFNP_WINDOWS_CXX:-x86_64-w64-mingw32-g++}
python3 - "$sender_syntax_sdk" <<'PY'
import hashlib
from pathlib import Path
import sys
root = Path(sys.argv[1])
for name, expected in {
    'nvEncodeAPI.h': 'be0c29a41ae3260fd016a9001fd959e747bac93ad21411759736abba4ffdea86',
    'openvr_driver.h': '1036efe998d63e82d1d3db2b32a2f58df4a8eeaf5280f50aaf28220ff60a40ab',
}.items():
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
        raise SystemExit('Public SDK pin mismatch: ' + name)
PY
for sender_syntax_fixture in \
  tests/test_stream_bridge_sender_provider_context.cpp \
  tests/test_stream_bridge_source_alpha_gpu.cpp \
  tests/test_stream_bridge_source_audit_gpu.cpp; do
  "$sender_syntax_cxx" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    -I"$sender_syntax_repo" -isystem "$sender_syntax_sdk" -fsyntax-only \
    "$sender_syntax_repo/$sender_syntax_fixture"
done
printf '%s\n' 'Windows sender fixture syntax preflight PASS (not GPU/runtime validation)'
