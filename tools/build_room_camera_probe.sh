#!/usr/bin/env bash
set -euo pipefail
# Build only. No device connection, installation, restart or camera acquisition.
if [[ $# != 2 ]]; then
    echo 'Usage: build_room_camera_probe.sh <reviewed-openvr-header-directory> <output-directory>' >&2
    exit 2
fi
room_probe_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
room_probe_sdk=$(realpath -- "$1")
room_probe_output=$(realpath -m -- "$2")
room_probe_expected=3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7
[[ $(sha256sum -- "$room_probe_sdk/openvr_capi.h" | cut -d ' ' -f 1) == "$room_probe_expected" ]] || {
    echo 'Unreviewed public OpenVR header.' >&2; exit 2;
}
mkdir -p -- "$room_probe_output"
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I "$room_probe_root" \
    "$room_probe_root/tests/room_camera_probe_test.cpp" -o "$room_probe_output/room-camera-test"
"$room_probe_output/room-camera-test"
aarch64-linux-gnu-g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    -isystem "$room_probe_sdk" -static-libstdc++ -static-libgcc -fvisibility=hidden -Wl,--exclude-libs,ALL \
    -ffile-prefix-map="$room_probe_root"=. -s "$room_probe_root/probe/room_camera_frame.cpp" \
    -o "$room_probe_output/sfnp-room-camera-frame" -ldl
sha256sum -- "$room_probe_output/sfnp-room-camera-frame"
