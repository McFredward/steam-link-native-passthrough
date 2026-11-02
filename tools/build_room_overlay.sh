#!/usr/bin/env bash
set -euo pipefail
# Local build only; no installation, camera access, or device connection.
if [[ $# != 8 ]]; then
    echo 'Usage: build_room_overlay.sh <OpenVR headers> <JSON include> <DejaVuSans.ttf> <output> <Vulkan headers> <OpenXR headers> <host vision module> <ARM vision module>' >&2
    exit 2
fi
room_build_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
room_build_sdk=$(realpath -- "$1")
room_build_json=$(realpath -- "$2")
room_build_font=$(realpath -- "$3")
room_build_output=$(realpath -m -- "$4")
room_build_vulkan=$(realpath -- "$5")
room_build_xr=$(realpath -- "$6")
room_build_host_vision=$(realpath -- "$7")
room_build_arm_vision=$(realpath -- "$8")
[[ $(sha256sum "$room_build_sdk/openvr_capi.h" | cut -d ' ' -f 1) == 3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7 ]]
[[ $(sha256sum "$room_build_json/nlohmann/json.hpp" | cut -d ' ' -f 1) == aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63 ]]
[[ $(sha256sum "$room_build_font" | cut -d ' ' -f 1) == ae7b7855e115a5966d8b1b3f80f254ccc117ec86f9965e202ee2940453837280 ]]
python3 -B - "$room_build_root/packaging/room-vulkan-headers.json" "$room_build_vulkan" <<'PYVULKAN'
from pathlib import Path
import sys,json,hashlib
for name,pin in json.loads(Path(sys.argv[1]).read_text())["files"].items():
    raw=(Path(sys.argv[2])/name).read_bytes()
    if len(raw)!=pin["bytes"] or hashlib.sha256(raw).hexdigest()!=pin["sha256"]:
        raise SystemExit("Vulkan header differs from its reviewed build input: "+name)
PYVULKAN
python3 -B "$room_build_root/tools/fetch_room_xr_headers.py" "$room_build_xr"
mkdir -p -- "$room_build_output"
python3 -B "$room_build_root/tools/build_room_font.py" "$room_build_font" "$room_build_output/room_font.h"
python3 -B "$room_build_root/tools/build_room_scene_shaders.py" "$room_build_output"
room_build_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I "$room_build_output" -I "$room_build_root"
    -isystem "$room_build_sdk" -isystem "$room_build_json" -isystem "$room_build_vulkan")
g++ "${room_build_flags[@]}" "$room_build_root/room_scan/native_overlay.cpp" \
    -o "$room_build_output/sfnp-room-overlay-host" -ldl -pthread
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_native_test.cpp" -o "$room_build_output/room-native-test"
"$room_build_output/room-native-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_projection_test.cpp" -o "$room_build_output/room-projection-test"
"$room_build_output/room-projection-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_surface_stereo_test.cpp" -o "$room_build_output/room-surface-stereo-test"
"$room_build_output/room-surface-stereo-test" "$room_build_host_vision"
python3 -B "$room_build_root/tests/test_room_fusion_native.py" "$room_build_host_vision" -q
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_scene_fixture.cpp" -o "$room_build_output/room-scene-fixture"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_scene_test.cpp" -o "$room_build_output/room-scene-test" -pthread
"$room_build_output/room-scene-test" "$room_build_output/room-scene-fixture"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_scene_mesh_test.cpp" -o "$room_build_output/room-scene-mesh-test"
"$room_build_output/room-scene-mesh-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_scene_snapshot_test.cpp" -o "$room_build_output/room-scene-snapshot-test" -pthread
"$room_build_output/room-scene-snapshot-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_camera_source_test.cpp" -o "$room_build_output/room-camera-source-test"
"$room_build_output/room-camera-source-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_arcturus_geometry_test.cpp" -o "$room_build_output/room-arcturus-geometry-test"
"$room_build_output/room-arcturus-geometry-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_arcturus_timing_test.cpp" -o "$room_build_output/room-arcturus-timing-test"
"$room_build_output/room-arcturus-timing-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_registration_test.cpp" -o "$room_build_output/room-registration-test"
"$room_build_output/room-registration-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_dashboard_test.cpp" -o "$room_build_output/room-dashboard-test" -pthread
"$room_build_output/room-dashboard-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_input_test.cpp" -o "$room_build_output/room-input-test"
"$room_build_output/room-input-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_pipe_test.cpp" -o "$room_build_output/room-pipe-test" -pthread
"$room_build_output/room-pipe-test" "$room_build_root" "$room_build_host_vision"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_gpu_test.cpp" -o "$room_build_output/room-gpu-test" -ldl
"$room_build_output/room-gpu-test"
room_build_xr_sources=("$room_build_root/room_scan/native_xr_renderer.cpp" "$room_build_root/room_scan/native_xr_api.cpp" "$room_build_root/room_scan/native_xr_graphics.cpp")
g++ "${room_build_flags[@]}" -Wno-missing-field-initializers -isystem "$room_build_xr" \
    "$room_build_root/tests/room_scene_gpu_test.cpp" "$room_build_root/room_scan/native_xr_graphics.cpp" \
    -o "$room_build_output/room-scene-gpu-test" -ldl
"$room_build_output/room-scene-gpu-test"
g++ "${room_build_flags[@]}" -Wno-missing-field-initializers -isystem "$room_build_xr" \
    "${room_build_xr_sources[@]}" -o "$room_build_output/sfnp-room-renderer-host" -ldl -pthread
aarch64-linux-gnu-g++ "${room_build_flags[@]}" -Wno-missing-field-initializers -isystem "$room_build_xr" \
    -static-libstdc++ -static-libgcc -fvisibility=hidden -Wl,--exclude-libs,ALL -ffile-prefix-map="$room_build_root"=. -s \
    "${room_build_xr_sources[@]}" -o "$room_build_output/sfnp-room-renderer" -ldl -pthread
aarch64-linux-gnu-g++ "${room_build_flags[@]}" -static-libstdc++ -static-libgcc -fvisibility=hidden \
    -Wl,--exclude-libs,ALL -ffile-prefix-map="$room_build_root"=. -s \
    "$room_build_root/room_scan/native_overlay.cpp" -o "$room_build_output/sfnp-room-overlay" -ldl -pthread
if [[ "$room_build_arm_vision" != "$room_build_output/libsfnp-room-vision.so" ]]; then
    cp -- "$room_build_arm_vision" "$room_build_output/libsfnp-room-vision.so"
fi
chmod 700 "$room_build_output/sfnp-room-overlay" "$room_build_output/sfnp-room-renderer"
chmod 600 "$room_build_output/libsfnp-room-vision.so"
sha256sum "$room_build_output/sfnp-room-overlay" "$room_build_output/sfnp-room-renderer" "$room_build_output/libsfnp-room-vision.so"
