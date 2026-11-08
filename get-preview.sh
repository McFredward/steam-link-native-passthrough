#!/usr/bin/env bash
# Compatibility alias for the same paired-only setup; device changes require a selected setup action.
set -euo pipefail
for tool in curl sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Required tool missing: $tool" >&2; exit 2; }
done
sfnp_preview_stage=$(mktemp -d)
trap 'rm -rf -- "$sfnp_preview_stage"' EXIT
curl --fail --silent --show-error --proto '=https' --max-time 30 --max-filesize 65536 \
    'https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/29171267a2e3499c0ec7a5ab51127cf828661c60/get.sh' \
    --output "$sfnp_preview_stage/get.sh"
printf '%s  %s\n' 'd3417bc598df054a53c9fd93e12bfac1f9b3fd904b5e70109064d823c597c187' "$sfnp_preview_stage/get.sh" | sha256sum --check --status
bash "$sfnp_preview_stage/get.sh" --bridge-preview
