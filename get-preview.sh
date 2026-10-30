#!/usr/bin/env bash
# Compatibility alias for the same paired-only setup; device changes require a selected setup action.
set -euo pipefail
for tool in curl sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Required tool missing: $tool" >&2; exit 2; }
done
sfnp_preview_stage=$(mktemp -d)
trap 'rm -rf -- "$sfnp_preview_stage"' EXIT
curl --fail --silent --show-error --proto '=https' --max-time 30 --max-filesize 65536 \
    'https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/fcefb61275c3fbee72931d2b28935cf87a5e6fcd/get.sh' \
    --output "$sfnp_preview_stage/get.sh"
printf '%s  %s\n' '70c67b4e22d788ea4c0d345a373b083aba01d31c969a1bb175f2775873949881' "$sfnp_preview_stage/get.sh" | sha256sum --check --status
bash "$sfnp_preview_stage/get.sh" --bridge-preview
