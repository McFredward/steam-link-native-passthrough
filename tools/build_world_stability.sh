#!/usr/bin/env bash
set -euo pipefail
# Build only: never contact a headset or start/modify SteamVR.
if [[ $# != 2 ]]; then
    echo "Usage: $0 <reviewed-openvr-header-directory> <output-directory>" >&2
    exit 2
fi
stability_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
stability_sdk=$(realpath -- "$1")
stability_output=$(realpath -m -- "$2")
stability_expected=3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7
[[ $(sha256sum -- "$stability_sdk/openvr_capi.h" | cut -d ' ' -f 1) == "$stability_expected" ]] || {
    echo 'Unreviewed public OpenVR header.' >&2; exit 2;
}
mkdir -p -- "$stability_output"
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I "$stability_root" \
    "$stability_root/tests/world_stability_test.cpp" -o "$stability_output/world-stability-test"
"$stability_output/world-stability-test"
aarch64-linux-gnu-g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    -isystem "$stability_sdk" -static-libstdc++ -static-libgcc -fvisibility=hidden -Wl,--exclude-libs,ALL \
    -ffile-prefix-map="$stability_root"=. -s "$stability_root/probe/world_stability_frame.cpp" \
    -o "$stability_output/sfnp-world-stability-frame" -ldl
sha256sum -- "$stability_output/sfnp-world-stability-frame"
