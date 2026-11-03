#!/usr/bin/env bash
# Offline development build only. No SDK downloads, registration or installation.
# Builds the OpenVR provider and the OpenXR API layer installed beside it; the
# SDK directory holds the pinned OpenVR/NVENC headers and OpenXR-SDK 1.1.63's
# include/openxr headers under openxr/.
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo 'Usage: stream-bridge-sender-provider-build.sh SDK_DIRECTORY PROTOCOL_SOURCE_ROOT OUTPUT_DIRECTORY' >&2
  exit 2
fi
sender_sdk=$(realpath -e -- "$1")
sender_protocol=$(realpath -e -- "$2")
sender_output=$(realpath -m -- "$3")
sender_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 - "$sender_sdk" <<'PY'
from pathlib import Path
import hashlib,sys
root=Path(sys.argv[1])
for name,expected in {
 'nvEncodeAPI.h':'be0c29a41ae3260fd016a9001fd959e747bac93ad21411759736abba4ffdea86',
 'openvr_driver.h':'1036efe998d63e82d1d3db2b32a2f58df4a8eeaf5280f50aaf28220ff60a40ab',
 'openxr/openxr.h':'17e8e334a7ad0c6933b31143330d4771010e393df5d5d9d08566eb15bb171a15',
 'openxr/openxr_platform_defines.h':'a458ba5777415f2de518fdcdcc87fcf8651ba0468c9f69a101378672aad80946',
 'openxr/openxr_loader_negotiation.h':'ebcca0946b4b738ebee41943b821abe4df518b3a37766e0ce6883bcefce65124',
}.items():
 if hashlib.sha256((root/name).read_bytes()).hexdigest()!=expected:
  raise SystemExit('Explicit SDK header does not match the reviewed pin: '+name)
PY
mkdir -p -- "$sender_output"
x86_64-w64-mingw32-gcc -std=c99 -O2 -w -DZSTD_DISABLE_ASM -DZSTDLIB_VISIBILITY= \
  -c "$sender_protocol/third_party/zstd/zstd.c" -o "$sender_output/sfnp-zstd.o"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I"$sender_repo" -I"$sender_protocol" -isystem "$sender_sdk" \
  "$sender_repo/pc/stream-bridge-sender-provider.cpp" \
  "$sender_repo/pc/stream-bridge-sender-provider-core.cpp" \
  "$sender_repo/pc/stream-bridge-sender-provider-directmode.cpp" \
  "$sender_repo/pc/stream-bridge-sender-provider-runtime.cpp" \
  "$sender_repo/pc/stream-bridge-sender-provider-coverage.cpp" \
  "$sender_repo/pc/stream-bridge-mask-capture.cpp" \
  "$sender_repo/pc/stream-bridge-sender-provider-coverage-render.cpp" \
  "$sender_repo/pc/stream-bridge-mask-hevc.cpp" \
  "$sender_repo/pc/stream-bridge-mask-hevc-surface.cpp" \
  "$sender_repo/pc/stream-bridge-sender-provider-sourceipc.cpp" \
  "$sender_repo/pc/stream-bridge-sender.cpp" \
  "$sender_protocol/bridge/stream_bridge_protocol.cpp" \
  "$sender_protocol/bridge/stream_bridge_auth.cpp" \
  "$sender_protocol/bridge/stream_bridge_frame.cpp" \
  "$sender_protocol/bridge/stream_bridge_hevc_coverage.cpp" \
  "$sender_protocol/bridge/stream_bridge_compression.cpp" \
  "$sender_protocol/bridge/stream_bridge_capacity.cpp" \
  "$sender_protocol/bridge/stream_bridge_credentials.cpp" \
  "$sender_protocol/bridge/stream_bridge_control.cpp" \
  "$sender_protocol/receiver/stream_bridge_receiver.cpp" \
  "$sender_protocol/bridge/hevc_bridge_sei.cpp" \
  "$sender_output/sfnp-zstd.o" \
  -shared -s -Wl,--no-insert-timestamp -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic \
  -lwintrust -lcrypt32 -lbcrypt -lws2_32 -ladvapi32 -lole32 -lshell32 -ld3d11 -ld3dcompiler -ldxgi -luuid -o "$sender_output/driver_steam_link_native_passthrough.dll"
cp -- "$sender_repo/pc/stream-bridge-sender-provider.vrdrivermanifest" "$sender_output/driver.vrdrivermanifest"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers \
  -DXR_USE_PLATFORM_WIN32 -iquote "$sender_repo" -isystem "$sender_sdk" \
  "$sender_repo/openxr/openxr_layer.cpp" "$sender_repo/openxr/openxr_source_windows.cpp" \
  -shared -s -Wl,--no-insert-timestamp -static -lbcrypt -o "$sender_output/XrApiLayer_sfnp_passthrough.dll"
cp -- "$sender_repo/openxr/XrApiLayer_sfnp_passthrough.json" "$sender_output/XrApiLayer_sfnp_passthrough.json"
sha256sum -- "$sender_output/driver_steam_link_native_passthrough.dll" "$sender_output/XrApiLayer_sfnp_passthrough.dll"
