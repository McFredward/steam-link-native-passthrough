#!/usr/bin/env bash
# Build only owned Windows fixtures with the distribution's GNU ABI/link policy.
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then exit 2; fi
audit_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
audit_output=$(realpath -m -- "$1")
audit_cxx=${2:-x86_64-w64-mingw32-g++}
mkdir -p -- "$audit_output"
audit_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I"$audit_repo" -I"$audit_repo/pc" -s -Wl,--no-insert-timestamp -static-libgcc -static-libstdc++)
audit_runtime=(-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
audit_shaders="$audit_repo/pc/stream-bridge-sender-provider-shaders.cpp"
audit_observer="$audit_repo/pc/stream-bridge-source-audit-observer.cpp"
"$audit_cxx" "${audit_flags[@]}" "$audit_repo/tests/test_stream_bridge_source_audit_gpu.cpp" "$audit_shaders" "${audit_runtime[@]}" -ld3d11 -ld3dcompiler -ldxgi -luuid -o "$audit_output/audit-gpu.exe"
"$audit_cxx" "${audit_flags[@]}" "$audit_repo/tests/test_stream_bridge_source_audit_loader.cpp" "${audit_runtime[@]}" -ld3d11 -ldxgi -luuid -o "$audit_output/vrserver.exe"
python3 "$audit_repo/tests/make_source_audit_controls.py" "$audit_output"
for audit_variant in native no-retry no-identity; do
  if [[ $audit_variant == native ]]; then
    audit_directory=$audit_output
    audit_loader="$audit_repo/pc/stream-bridge-source-audit-windows.cpp"
  else
    audit_directory="$audit_output/$audit_variant"
    audit_loader="$audit_directory/loader.cpp"
    cp -- "$audit_output/vrserver.exe" "$audit_directory/vrserver.exe"
  fi
  "$audit_cxx" "${audit_flags[@]}" -shared "$audit_repo/tests/source_audit_loader_fixture.cpp" "$audit_loader" "$audit_observer" "$audit_shaders" "${audit_runtime[@]}" -ladvapi32 -ld3d11 -ldxgi -luuid -o "$audit_directory/audit_fixture.dll"
  cp -- "$audit_directory/vrserver.exe" "$audit_directory/vrcompositor.exe"
  mkdir -p -- "$audit_directory/decoy"
  cp -- "$audit_directory/vrserver.exe" "$audit_directory/decoy/vrcompositor.exe"
done
