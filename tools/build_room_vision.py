#!/usr/bin/env python3
"""Build the pinned private CPU vision module; never install or contact a headset."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def license_notices(sources):
    text = sources['opencv'].joinpath('LICENSE').read_text()+"\n\n"
    text += "Additional upstream notices for the linked CPU vision modules.\n"
    seen = set()
    for repository, modules in (("opencv", ("core", "imgproc", "calib3d")), ("opencv_contrib", ("ximgproc",))):
        for module in modules:
            for path in sorted((sources[repository]/"modules"/module).rglob("*")):
                if not path.is_file() or path.suffix not in (".h", ".hpp", ".cpp", ".inl"):
                    continue
                raw = path.read_bytes()
                try:
                    body = raw.decode("utf-8")
                except UnicodeDecodeError:
                    body = raw.decode("latin-1")
                comments = re.findall(r"/\*[\s\S]*?\*/|(?:^[ \t]*//[^\n]*(?:\n|$))+", body, re.MULTILINE)
                for comment in comments:
                    if not re.search(r"copyright|redistribution|SPDX|license|permission", comment, re.IGNORECASE):
                        continue
                    comment = comment.strip()
                    if comment in seen:
                        continue
                    seen.add(comment)
                    text += "\n\n"+repository+"/"+path.relative_to(sources[repository]).as_posix()+"\n"+comment+"\n"
    text += "\n\nBundled zlib notice:\n\n"+sources['opencv'].joinpath('3rdparty/zlib/LICENSE').read_text()
    return text


def source(cache, name, pin):
    archive = cache/(name+".tar.gz")
    if not archive.exists():
        partial = archive.with_suffix(".partial")
        try:
            with urllib.request.urlopen(pin["archive_url"], timeout=60) as incoming, partial.open("wb") as out:
                size = 0
                while block := incoming.read(1024*1024):
                    size += len(block)
                    if size > pin["archive_bytes"]:
                        raise ValueError("Vision source archive exceeds its pin")
                    out.write(block)
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    if archive.stat().st_size != pin["archive_bytes"] or digest(archive) != pin["archive_sha256"]:
        raise ValueError("Vision source archive differs: "+name)
    target = cache/name
    with tarfile.open(archive, "r:gz") as package:
        members, total, files = [], 0, set()
        for member in package.getmembers():
            parts = PurePosixPath(member.name).parts
            if not parts or ".." in parts or member.name.startswith("/") or not (member.isdir() or member.isfile()):
                raise ValueError("Unsupported vision source entry")
            member.name = str(PurePosixPath(*parts[1:]))
            if member.name == ".":
                continue
            total += member.size
            if total > 768*1024*1024:
                raise ValueError("Expanded vision source exceeds its budget")
            members.append(member)
            if member.isfile():
                files.add(member.name)
        if not target.exists():
            temporary = cache/(name+".extracting")
            if temporary.exists():
                shutil.rmtree(temporary)
            temporary.mkdir()
            try:
                for member in members:
                    package.extract(member, temporary, filter="data")
                temporary.replace(target)
            finally:
                if temporary.exists():
                    shutil.rmtree(temporary)
        # Check all pinned archive bytes, including CMake inputs. An existing
        # source directory is a cache, not evidence that its contents are intact.
        for member in members:
            if not member.isfile():
                continue
            path = target/member.name
            if path.is_symlink() or not path.is_file() or path.stat().st_size != member.size:
                raise ValueError("Vision source file differs: "+member.name)
            expected = package.extractfile(member).read()
            if hashlib.sha256(expected).digest() != hashlib.sha256(path.read_bytes()).digest():
                raise ValueError("Vision source file differs: "+member.name)
        for path in target.rglob("*"):
            relative = path.relative_to(target).as_posix()
            if path.is_file() and relative not in files and not relative.startswith(".cache/"):
                raise ValueError("Unpinned extra vision source: "+relative)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_cache", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--arch", choices=("host", "aarch64"), required=True)
    parser.add_argument("--opencv-build", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("Use 1 to 16 build workers")
    cache, output = args.source_cache.resolve(), args.output.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    spec = json.loads((ROOT/"packaging/room-vision.json").read_text())
    sources = {name: source(cache, name, pin) for name, pin in spec["sources"].items()}
    if (ROOT/'room_scan/licenses/OpenCV.txt').read_text() != license_notices(sources):
        raise ValueError("OpenCV notices differ from the pinned source modules")
    build = args.opencv_build.resolve() if args.opencv_build else output/("opencv-"+args.arch)
    configure = ["cmake", "-S", str(sources["opencv"]), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections",
        "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections", "-DBUILD_LIST=calib3d,ximgproc",
        "-DOPENCV_EXTRA_MODULES_PATH="+str(sources["opencv_contrib"]/"modules"), "-DBUILD_ZLIB=ON"]
    for flag in ("BUILD_SHARED_LIBS", "BUILD_TESTS", "BUILD_PERF_TESTS", "BUILD_EXAMPLES", "BUILD_opencv_apps",
                 "BUILD_opencv_python3", "BUILD_opencv_dnn", "BUILD_JAVA", "OPENCV_ENABLE_NONFREE", "OPENCV_TRACE",
                 "WITH_OPENCL", "WITH_IPP", "WITH_TBB", "WITH_LAPACK", "WITH_ITT", "WITH_CUDA", "WITH_V4L",
                 "WITH_FFMPEG", "WITH_GSTREAMER", "WITH_GTK", "WITH_QT", "WITH_OPENEXR", "WITH_WEBP", "WITH_AVIF",
                 "WITH_OPENJPEG", "WITH_TIFF", "WITH_JASPER", "WITH_PNG", "WITH_JPEG", "WITH_PROTOBUF",
                 "WITH_KLEIDICV", "WITH_CAROTENE", "WITH_OBSENSOR", "WITH_IMGCODEC_HDR", "WITH_IMGCODEC_SUNRASTER",
                 "WITH_IMGCODEC_PXM", "WITH_IMGCODEC_PFM", "WITH_IMGCODEC_GIF"):
        configure.append("-D"+flag+"=OFF")
    if args.arch == "aarch64":
        toolchain = output/"vision-toolchain.cmake"
        toolchain.write_text("set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR aarch64)\n"
                             "set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)\nset(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)\n")
        configure.append("-DCMAKE_TOOLCHAIN_FILE="+str(toolchain))
    with (output/"vision-build.log").open("w") as log:
        print("Verified vision sources; building OpenCV for "+args.arch, flush=True)
        subprocess.run(configure, check=True, stdout=log, stderr=subprocess.STDOUT)
        subprocess.run(["cmake", "--build", str(build), "--parallel", str(args.jobs)], check=True, stdout=log, stderr=subprocess.STDOUT)
        compiler = "aarch64-linux-gnu-g++" if args.arch == "aarch64" else "g++"
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-fPIC", "-fvisibility=hidden",
            "-ffunction-sections", "-fdata-sections", "-ffile-prefix-map="+str(ROOT)+"=.", "-I", str(build)]
        for name in ("core", "imgproc", "calib3d", "features2d", "flann"):
            command += ["-isystem", str(sources["opencv"]/"modules"/name/"include")]
        command += ["-isystem", str(sources["opencv_contrib"]/"modules/ximgproc/include"), "-shared",
            str(ROOT/"room_scan/vision_stereo.cpp"), str(ROOT/"room_scan/vision_fusion.cpp"), "-L", str(build/"lib"),
            "-Wl,--start-group", "-lopencv_ximgproc", "-lopencv_calib3d", "-lopencv_imgproc", "-lopencv_core",
            str(build/"3rdparty/lib/libzlib.a"), "-Wl,--end-group", "-Wl,--gc-sections", "-Wl,--exclude-libs,ALL",
            "-Wl,--version-script="+str(ROOT/"room_scan/vision.exports"), "-Wl,-soname,libsfnp-room-vision.so", "-Wl,-z,relro,-z,now",
            "-static-libstdc++", "-static-libgcc", "-s", "-ldl", "-pthread", "-o", str(output/"libsfnp-room-vision.so")]
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    artifact = output/"libsfnp-room-vision.so"
    artifact.chmod(0o600)
    receipt = {"schema": "sfnp-room-vision-artifact-v1", "architecture": args.arch, "build_spec_sha256": digest(ROOT/"packaging/room-vision.json"),
               "source_archives": spec["sources"], "bytes": artifact.stat().st_size, "sha256": digest(artifact)}
    (output/"vision-artifact.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print(json.dumps(receipt), flush=True)


if __name__ == "__main__":
    main()
