#!/usr/bin/env python3
"""Package/verify an owned standalone camera inventory; never contact devices."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK_SHA256 = "3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7"
SOURCES = ("probe/room_camera_probe_core.h", "probe/room_camera_frame.cpp",
           "tests/room_camera_probe_test.cpp", "tools/build_room_camera_probe.sh",
           "tools/package_room_camera_probe.py", "docs/development/ROOM-CAPTURE.md")
MEMBERS = {"sfnp-room-camera-frame", "ROOM-CAPTURE.md", "LICENSE", "manifest.json"}


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def sources():
    return {name: digest((ROOT/name).read_bytes()) for name in SOURCES}


def verify(path):
    raw = path.read_bytes()
    receipt = json.loads(path.with_suffix(".receipt.json").read_text(encoding="utf-8"))
    if not 0 < len(raw) <= 4*1024*1024 or receipt != {
            "schema": "sfnp-room-camera-archive-v1", "bytes": len(raw), "sha256": digest(raw)}:
        raise ValueError("Camera archive differs from its receipt")
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) != len(MEMBERS) or {e.filename for e in entries} != MEMBERS:
            raise ValueError("Unexpected camera inventory payload")
        if any(e.is_dir() or e.file_size > 4*1024*1024 for e in entries):
            raise ValueError("Unbounded camera inventory entry")
        manifest = json.loads(archive.read("manifest.json"))
        if (manifest.get("schema") != "sfnp-room-camera-artifact-v1"
                or manifest.get("source_files_sha256") != sources()
                or manifest.get("public_openvr_capi_sha256") != SDK_SHA256
                or manifest.get("hardware_validation") != "not-run"
                or manifest.get("camera_images_read") is not False
                or manifest.get("native_companions_modified") is not False
                or set(manifest["files"]) != MEMBERS-{"manifest.json"}):
            raise ValueError("Camera inventory scope or source snapshot differs")
        for name, expected in manifest["files"].items():
            content = archive.read(name)
            if expected != {"bytes": len(content), "sha256": digest(content)}:
                raise ValueError("Camera inventory member differs")
    print("PASS standalone camera inventory archive, exact files and source hashes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--binary", type=Path)
    mode.add_argument("--verify", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify)
        return
    if not args.output:
        parser.error("--output is required with --binary")
    binary = args.binary.read_bytes()
    if not 64 <= len(binary) <= 4*1024*1024 or binary[:6] != b"\x7fELF\x02\x01" or int.from_bytes(binary[18:20], "little") != 183:
        parser.error("The built native AArch64 inventory binary is required")
    data = {"sfnp-room-camera-frame": binary, "LICENSE": (ROOT/"LICENSE").read_bytes(),
            "ROOM-CAPTURE.md": (ROOT/"docs/development/ROOM-CAPTURE.md").read_bytes()}
    versions = subprocess.check_output(["aarch64-linux-gnu-readelf", "--version-info", str(args.binary)], text=True)
    manifest = {"schema": "sfnp-room-camera-artifact-v1", "source_files_sha256": sources(),
                "public_openvr_capi_sha256": SDK_SHA256, "hardware_validation": "not-run",
                "camera_images_read": False, "native_companions_modified": False,
                "command": "--inspect --openvr /canonical/installed/libopenvr_api.so",
                "cross_compiler": subprocess.check_output(["aarch64-linux-gnu-g++", "-dumpfullversion", "-dumpversion"], text=True).strip(),
                "required_glibc": sorted(set(re.findall(r"Name: (GLIBC_[0-9.]+)", versions))),
                "files": {name: {"bytes": len(raw), "sha256": digest(raw)} for name, raw in data.items()}}
    data["manifest.json"] = (json.dumps(manifest, indent=2)+"\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, raw in sorted(data.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if name == "sfnp-room-camera-frame" else 0o100644) << 16
            archive.writestr(info, raw)
    raw = args.output.read_bytes()
    receipt = {"schema": "sfnp-room-camera-archive-v1", "bytes": len(raw), "sha256": digest(raw)}
    args.output.with_suffix(".receipt.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
    verify(args.output)


if __name__ == "__main__":
    main()
