#!/usr/bin/env bash
set -euo pipefail
# Local build only; no installation, camera access, or device connection.
if [[ $# != 4 ]]; then
    echo 'Usage: build_room_overlay.sh <openvr-header-directory> <json-single-include-directory> <DejaVuSans.ttf> <output>' >&2
    exit 2
fi
room_build_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
room_build_sdk=$(realpath -- "$1")
room_build_json=$(realpath -- "$2")
room_build_font=$(realpath -- "$3")
room_build_output=$(realpath -m -- "$4")
[[ $(sha256sum "$room_build_sdk/openvr_capi.h" | cut -d ' ' -f 1) == 3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7 ]]
[[ $(sha256sum "$room_build_json/nlohmann/json.hpp" | cut -d ' ' -f 1) == aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63 ]]
[[ $(sha256sum "$room_build_font" | cut -d ' ' -f 1) == ae7b7855e115a5966d8b1b3f80f254ccc117ec86f9965e202ee2940453837280 ]]
mkdir -p -- "$room_build_output"
python3 -B "$room_build_root/tools/build_room_font.py" "$room_build_font" "$room_build_output/room_font.h"
room_build_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I "$room_build_output" -I "$room_build_root"
    -isystem "$room_build_sdk" -isystem "$room_build_json")
g++ "${room_build_flags[@]}" "$room_build_root/room_scan/native_overlay.cpp" \
    -o "$room_build_output/sfnp-room-overlay-host" -ldl -pthread
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_native_test.cpp" -o "$room_build_output/room-native-test"
"$room_build_output/room-native-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_camera_source_test.cpp" -o "$room_build_output/room-camera-source-test"
"$room_build_output/room-camera-source-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_registration_test.cpp" -o "$room_build_output/room-registration-test"
"$room_build_output/room-registration-test"
g++ "${room_build_flags[@]}" "$room_build_root/tests/room_pipe_test.cpp" -o "$room_build_output/room-pipe-test" -pthread
"$room_build_output/room-pipe-test" "$room_build_root"
aarch64-linux-gnu-g++ "${room_build_flags[@]}" -static-libstdc++ -static-libgcc -fvisibility=hidden \
    -Wl,--exclude-libs,ALL -ffile-prefix-map="$room_build_root"=. -s \
    "$room_build_root/room_scan/native_overlay.cpp" -o "$room_build_output/sfnp-room-overlay" -ldl -pthread
sha256sum "$room_build_output/sfnp-room-overlay"
