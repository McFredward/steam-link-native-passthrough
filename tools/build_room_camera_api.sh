#!/usr/bin/env bash
set -euo pipefail
# Manual prototype only. This never installs, starts or contacts a headset.
if [[ $# != 2 ]]; then
    echo 'Usage: build_room_camera_api.sh <JSON-single-include-directory> <output>' >&2
    exit 2
fi
room_camera_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
room_camera_json=$(realpath -- "$1")
room_camera_out=$(realpath -m -- "$2")
[[ $(sha256sum "$room_camera_json/nlohmann/json.hpp" | cut -d ' ' -f 1) == aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63 ]]
mkdir -p -- "$room_camera_out"
cd -- "$room_camera_root"
room_camera_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I "$room_camera_root" -isystem "$room_camera_json")
g++ "${room_camera_flags[@]}" "$room_camera_root/tests/room_camera_policy_test.cpp" -o "$room_camera_out/room-camera-policy-test"
"$room_camera_out/room-camera-policy-test"
g++ "${room_camera_flags[@]}" "$room_camera_root/tests/room_camera_api_test.cpp" -o "$room_camera_out/room-camera-api-test"
"$room_camera_out/room-camera-api-test"
g++ "${room_camera_flags[@]}" "$room_camera_root/tests/room_camera_luma_test.cpp" -o "$room_camera_out/room-camera-luma-test"
"$room_camera_out/room-camera-luma-test"
g++ "${room_camera_flags[@]}" "$room_camera_root/tests/room_camera_metadata_test.cpp" -o "$room_camera_out/room-camera-metadata-test"
"$room_camera_out/room-camera-metadata-test"
g++ "${room_camera_flags[@]}" "$room_camera_root/tests/room_camera_snapshot_test.cpp" -o "$room_camera_out/room-camera-snapshot-test"
"$room_camera_out/room-camera-snapshot-test"
g++ "${room_camera_flags[@]}" "$room_camera_root/tests/room_camera_exposure_test.cpp" -o "$room_camera_out/room-camera-exposure-test"
"$room_camera_out/room-camera-exposure-test"
g++ "${room_camera_flags[@]}" "$room_camera_root/tests/room_camera_owner_test.cpp" -o "$room_camera_out/room-camera-owner-test"
"$room_camera_out/room-camera-owner-test"
g++ "${room_camera_flags[@]}" "$room_camera_root/probe/room_camera_native.cpp" -o "$room_camera_out/sfnp-room-camera-native-host"
aarch64-linux-gnu-g++ "${room_camera_flags[@]}" -static-libstdc++ -static-libgcc -s -ffile-prefix-map="$room_camera_root"=. \
    "$room_camera_root/probe/room_camera_native.cpp" -o "$room_camera_out/sfnp-room-camera-native"
sha256sum "$room_camera_out/sfnp-room-camera-native"
