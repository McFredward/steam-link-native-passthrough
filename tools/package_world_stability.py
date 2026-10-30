#!/usr/bin/env python3
"""Package the standalone diagnostic; no runtime patches, SDKs or device actions."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("probe/world_stability_core.h", "probe/world_stability_frame.cpp",
           "tests/world_stability_test.cpp", "tests/test_world_stability_report.py",
           "tools/analyze_world_stability.py", "tools/build_world_stability.sh",
           "tools/package_world_stability.py", "docs/development/WORLD-STABILITY.md")
SDK_SHA256 = "3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7"
MEMBERS = {"sfnp-world-stability-frame", "analyze_world_stability.py", "WORLD-STABILITY.md", "LICENSE", "manifest.json"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify(archive):
    payload = archive.read_bytes()
    receipt = json.loads(archive.with_suffix(".json").read_text(encoding="utf-8"))
    if not 0 < len(payload) <= 2 * 1024 * 1024 or receipt != {
            "schema": "sfnp-world-stability-archive-v1", "bytes": len(payload), "sha256": digest(payload)}:
        raise ValueError("Archive differs from its receipt")
    with zipfile.ZipFile(archive) as bundle:
        entries = bundle.infolist()
        if len(entries) != len(MEMBERS) or {entry.filename for entry in entries} != MEMBERS:
            raise ValueError("Unexpected diagnostic payload")
        if any(entry.is_dir() or entry.file_size > 4 * 1024 * 1024 for entry in entries):
            raise ValueError("Unbounded diagnostic entry")
        manifest = json.loads(bundle.read("manifest.json"))
        files = manifest["files"]
        if set(files) != MEMBERS - {"manifest.json"} or manifest.get("native_companions_modified") is not False:
            raise ValueError("This is not a standalone reference")
        for name, expected in files.items():
            value = bundle.read(name)
            if expected != {"bytes": len(value), "sha256": digest(value)}:
                raise ValueError("Changed diagnostic member")
        expected_sources = {name: digest((ROOT / name).read_bytes()) for name in SOURCES}
        if manifest["source_files_sha256"] != expected_sources or manifest["public_openvr_capi_sha256"] != SDK_SHA256:
            raise ValueError("Archive source snapshot differs from this checkout")
    print("PASS standalone reference archive, source snapshot and exact own-file inventory")


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
    if len(binary) < 64 or binary[:6] != b"\x7fELF\x02\x01" or int.from_bytes(binary[18:20], "little") != 183:
        parser.error("Expected the cross-built AArch64 ELF diagnostic")
    data = {
        "sfnp-world-stability-frame": binary,
        "analyze_world_stability.py": (ROOT / "tools/analyze_world_stability.py").read_bytes(),
        "WORLD-STABILITY.md": (ROOT / "docs/development/WORLD-STABILITY.md").read_bytes(),
        "LICENSE": (ROOT / "LICENSE").read_bytes(),
    }
    versions = subprocess.check_output(["aarch64-linux-gnu-readelf", "--version-info", str(args.binary)], text=True)
    manifest = {
        "schema": "sfnp-world-stability-artifact-v1", "diagnostic_only": True,
        "native_companions_modified": False, "requires_native_patch_reinstall": False,
        "source_files_sha256": {name: digest((ROOT / name).read_bytes()) for name in SOURCES},
        "public_openvr_capi_sha256": SDK_SHA256,
        "cross_compiler": subprocess.check_output(["aarch64-linux-gnu-g++", "-dumpfullversion", "-dumpversion"], text=True).strip(),
        "required_glibc": sorted(set(re.findall(r"Name: (GLIBC_[0-9.]+)", versions))),
        "files": {name: {"bytes": len(value), "sha256": digest(value)} for name, value in data.items()},
        "hardware_validation": "pending coordinated reference-visibility and motion test",
    }
    # The receipt describes reproducible inputs and pending hardware boundaries;
    # no software build flag can certify a physical headset image.
    data["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, value in sorted(data.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if name == "sfnp-world-stability-frame" else 0o100644) << 16
            archive.writestr(info, value)
    payload = args.output.read_bytes()
    receipt = {"schema": "sfnp-world-stability-archive-v1", "bytes": len(payload), "sha256": digest(payload)}
    args.output.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
