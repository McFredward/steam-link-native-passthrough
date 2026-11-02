#!/usr/bin/env python3
"""Fetch exact public build headers; no runtime binaries or headset changes."""
from pathlib import Path
import argparse
import hashlib
import json
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    spec = json.loads((ROOT/"packaging/room-openxr-headers.json").read_text())
    for name, pin in spec["files"].items():
        target = args.output/name
        if not target.exists():
            url = f"https://raw.githubusercontent.com/{spec['repository']}/{spec['commit']}/{pin['source']}"
            with urllib.request.urlopen(url, timeout=30) as response:
                raw = response.read(pin["bytes"]+1)
        else:
            raw = target.read_bytes()
        if len(raw) != pin["bytes"] or hashlib.sha256(raw).hexdigest() != pin["sha256"]:
            raise ValueError("Public XR header differs: "+name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
    print("Verified pinned public OpenXR headers")


if __name__ == "__main__":
    main()
