"""Owned PC reconstruction companion and bounded SSH room relay."""
from __future__ import annotations

import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import queue
import shutil
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import zipfile


def exchange_frame(request):
    """Runs only on the Frame through the installer's authenticated SSH session."""
    root = Path(f"/run/user/{os.getuid()}/sfnp-room-pc")
    try:
        info = root.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError("Private room relay required")
        fd = os.open(root/"session.json", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            record_info = os.fstat(stream.fileno())
            if not stat.S_ISREG(record_info.st_mode) or record_info.st_uid != os.getuid() or record_info.st_mode & 0o077:
                raise ValueError("Untrusted room relay descriptor")
            record = json.loads(stream.read(4097))
        if record["schema"] != "sfnp-room-pc-session-v1" or len(record["socket"]) > 100:
            raise ValueError("Unknown room relay")
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as connection:
            connection.settimeout(3)
            connection.connect("\0"+record["socket"])
            pid, uid, _ = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if pid != record["pid"] or uid != os.getuid():
                raise ValueError("Room relay owner changed")
            raw = json.dumps(request, separators=(",", ":"), allow_nan=False).encode()
            if len(raw) > 96000:
                raise ValueError("Room relay request exceeds budget")
            connection.sendall(raw)
            raw, _, flags, _ = connection.recvmsg(120000)
            if flags & socket.MSG_TRUNC:
                raise ValueError("Room relay response exceeds budget")
            return json.loads(raw)
    except (OSError, ValueError, KeyError, TypeError):
        return {"error": "Room Setup is not ready"}


def _download(item, cache, cancelled):
    destination = cache/item["filename"]
    if destination.is_file() and destination.stat().st_size == item["bytes"]:
        with destination.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() == item["sha256"]:
                return destination
    partial = destination.with_suffix(destination.suffix+".part")
    digest, total = hashlib.sha256(), 0
    try:
        with urllib.request.urlopen(item["url"], timeout=30) as response, partial.open("wb") as stream:
            while True:
                if cancelled():
                    raise InterruptedError("Room runtime download cancelled")
                chunk = response.read(1024*1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > item["bytes"]:
                    raise ValueError("Room runtime download exceeds pin")
                stream.write(chunk)
                digest.update(chunk)
        if total != item["bytes"] or digest.hexdigest() != item["sha256"]:
            raise ValueError("Room runtime integrity check failed")
        os.replace(partial, destination)
    finally:
        partial.unlink(missing_ok=True)
    return destination


def prepare_runtime(manifest_path, root, cancelled=lambda: False):
    """Private CUDA libraries only. No pip, system packages or driver changes."""
    manifest_raw = Path(manifest_path).read_bytes()
    manifest = json.loads(manifest_raw)
    if manifest["schema"] != "sfnp-room-pc-runtime-v1" or manifest["python"] != "3.14":
        raise ValueError("Unknown PC room runtime")
    pin = hashlib.sha256(manifest_raw).hexdigest()
    root = Path(root)
    runtime, cache = root/pin[:16], root/"downloads"
    root.mkdir(parents=True, exist_ok=True)
    cache.mkdir(exist_ok=True)
    receipt = runtime/"complete.json"
    if receipt.is_file():
        record = json.loads(receipt.read_text())
        if record.get("manifest") == pin and 0 < len(record.get("files", {})) < 20000:
            okay = True
            for name, expected in record["files"].items():
                path = runtime/name
                if (path.is_symlink() or not path.is_file() or path.stat().st_size != expected["bytes"]):
                    okay = False
                    break
                with path.open("rb") as stream:
                    if hashlib.file_digest(stream, "sha256").hexdigest() != expected["sha256"]:
                        okay = False
                        break
            if okay:
                return runtime
        raise ValueError("Private room runtime changed. Remove its cache and retry.")
    if shutil.disk_usage(root).free < 5*1024**3:
        raise ValueError("PC reconstruction needs 5 GB of free disk space")
    staging = Path(tempfile.mkdtemp(prefix=".room-runtime-", dir=root))
    try:
        records, expanded = {}, 0
        for item in manifest["wheels"]:
            wheel = _download(item, cache, cancelled)
            with zipfile.ZipFile(wheel) as archive:
                for member in archive.infolist():
                    name = PurePosixPath(member.filename)
                    if member.is_dir():
                        continue
                    if (name.is_absolute() or ".." in name.parts or "\\" in member.filename or ":" in member.filename
                            or stat.S_ISLNK(member.external_attr >> 16) or member.file_size > 700*1024**2):
                        raise ValueError("Unsafe runtime wheel member")
                    expanded += member.file_size
                    if expanded > 4*1024**3:
                        raise ValueError("Private room runtime exceeds budget")
                    raw = archive.read(member)
                    relative = "vendor/"+member.filename
                    target = staging/relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if target.exists() and target.read_bytes() != raw:
                        raise ValueError("Conflicting runtime wheel members")
                    target.write_bytes(raw)
                    records[relative] = {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
        model = _download(manifest["model"], cache, cancelled)
        shutil.copyfile(model, staging/"c-fast-foundationstereo.onnx")
        records["c-fast-foundationstereo.onnx"] = {key: manifest["model"][key] for key in ("bytes", "sha256")}
        for name, expected in manifest["licenses"].items():
            source = Path(manifest_path).parent/"room-licenses"/name
            raw = source.read_bytes()
            if len(raw) != expected["bytes"] or hashlib.sha256(raw).hexdigest() != expected["sha256"]:
                raise ValueError("Stereo model notices changed")
            destination = staging/"licenses"/name
            destination.parent.mkdir(exist_ok=True)
            destination.write_bytes(raw)
            records["licenses/"+name] = expected
        (staging/"complete.json").write_text(json.dumps({"manifest": pin, "files": records}, separators=(",", ":")))
        os.replace(staging, runtime)
        return runtime
    finally:
        if staging.exists():
            shutil.rmtree(staging)


class Companion:
    """Own exactly one GPU child. Closing the installer stops only that child."""
    def __init__(self, exchange, setup_dir, cache_root, ui, cancelled=lambda: False):
        self.exchange, self.setup_dir, self.cache_root = exchange, Path(setup_dir), Path(cache_root)
        self.ui, self.cancelled = ui, cancelled
        self.stop = threading.Event()
        self.process = None
        self.results = queue.Queue(maxsize=2)
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def close(self):
        self.stop.set()
        if self.process and self.process.poll() is None:
            self.process.terminate()
        self.thread.join(5)
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait(3)

    def _read(self):
        while not self.stop.is_set():
            raw = self.process.stdout.readline(12*1024*1024+1)
            if not raw:
                break
            try:
                if len(raw) > 12*1024*1024:
                    raise ValueError("PC result exceeds budget")
                self.results.put(json.loads(raw), timeout=3)
            except (ValueError, queue.Full):
                break

    def _read_errors(self):
        remaining = 256*1024
        with (self.cache_root/"worker.log").open("w", encoding="utf-8") as stream:
            while self.process and self.process.stderr:
                line = self.process.stderr.readline(4096)
                if not line:
                    break
                if remaining > 0:
                    stream.write(line[:remaining])
                    stream.flush()
                    remaining -= len(line)

    def _upload(self, value):
        raw = json.dumps(value, separators=(",", ":"), allow_nan=False).encode()
        response = self.exchange({"op": "begin", "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "binding": value["binding"], "epoch": value["capture_epoch"]})
        if response.get("error"):
            return False
        for offset in range(0, len(raw), 60000):
            if self.stop.is_set():
                return False
            if self.exchange({"op": "put", "offset": offset, "data": base64.b64encode(raw[offset:offset+60000]).decode()}).get("error"):
                return False
        return not self.exchange({"op": "commit"}).get("error")

    def _run(self):
        try:
            if sys.platform != "win32" or sys.version_info[:2] != (3, 14):
                raise ValueError("PC room reconstruction requires the bundled Windows Python 3.14 runtime")
            gpu = subprocess.run(["nvidia-smi", "--query-gpu=memory.free", "--format=csv,noheader,nounits"],
                capture_output=True, text=True, timeout=10, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
            free = int(gpu.stdout.splitlines()[0].strip())
            if free < 3000:
                raise ValueError("PC reconstruction needs at least 3 GB of free NVIDIA VRAM")
            width, height, limit = (320, 256, 25000) if free < 6000 else (512, 384, 50000) if free < 10000 else (640, 480, 60000)
            self.ui.say("bridge_room_pc_preparing")
            runtime = prepare_runtime(self.setup_dir/"room-pc-runtime.json", self.cache_root,
                                      lambda: self.stop.is_set() or self.cancelled())
            if self.stop.is_set():
                return
            self.ui.say("bridge_room_pc_warming")
            launcher = "import sys,runpy;sys.path.insert(0,sys.argv.pop(1));runpy.run_module('room_reconstruction.live_worker',run_name='__main__')"
            command = [sys.executable, "-B", "-c", launcher, str(self.setup_dir), "--runtime", str(runtime),
                       "--width", str(width), "--height", str(height), "--limit", str(limit)]
            self.process = subprocess.Popen(command, cwd=self.setup_dir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace", creationflags=subprocess.CREATE_NO_WINDOW)
            reader = threading.Thread(target=self._read, daemon=True)
            reader.start()
            threading.Thread(target=self._read_errors, daemon=True).start()
            ready = None
            while not self.stop.is_set():
                try:
                    ready = self.results.get(timeout=.2)
                    break
                except queue.Empty:
                    if self.process.poll() is not None:
                        raise RuntimeError("GPU worker stopped during warmup")
            if self.stop.is_set():
                return
            if ready.get("event") != "ready" or ready.get("provider") != "CUDAExecutionProvider":
                raise RuntimeError("GPU worker could not initialize")
            self.ui.say("bridge_room_pc_ready")
            busy, finish_key, last_heartbeat, seeded_map = False, None, 0., None
            while not self.stop.is_set() and not self.cancelled():
                if self.process.poll() is not None:
                    raise RuntimeError("GPU worker stopped")
                if time.monotonic()-last_heartbeat >= 2:
                    response = self.exchange({"op": "ready", "protocol": 1, "provider": "CUDAExecutionProvider"})
                    last_heartbeat = time.monotonic()
                    if response.get("error"):
                        self.stop.wait(.5)
                        continue
                try:
                    result = self.results.get_nowait()
                except queue.Empty:
                    result = None
                if result:
                    if result["event"] == "error":
                        raise RuntimeError("GPU reconstruction failed")
                    if result["event"] != "seeded":self._upload(result)
                    busy = False
                if busy:
                    self.stop.wait(.025)
                    continue
                state = self.exchange({"op": "take", "seed": seeded_map is None})
                if state.get("error"):
                    self.stop.wait(.5)
                    continue
                key = (state.get("map_id"), state.get("epoch"), str(state.get("binding")))
                if state.get("map_id") and seeded_map is None and state.get("seed_bytes"):
                    seed = bytearray()
                    if state['seed_bytes']>80000*48:raise ValueError('Retained room exceeds budget')
                    for offset in range(0,state['seed_bytes'],60000):
                        seed.extend(base64.b64decode(self.exchange({'op':'seed_chunk','offset':offset})['data'],validate=True))
                    message={'op':'seed','map_id':state['map_id'],'binding':state['binding'],'epoch':state['epoch'],
                        'gaussians':base64.b64encode(seed).decode()}
                    seeded_map=state['map_id']
                elif state["intent"] == "finish" and finish_key != key:
                    message = {"op": "finish", "map_id": state["map_id"], "binding": state["binding"], "epoch": state["epoch"]}
                    finish_key = key
                elif state["intent"] == "scan" and state["id"] is not None:
                    seeded_map=state['map_id']
                    packet = bytearray()
                    if not 0 < state["bytes"] <= 1100000:
                        raise ValueError("Camera relay packet exceeds budget")
                    for offset in range(0, state["bytes"], 60000):
                        chunk = self.exchange({"op": "chunk", "id": state["id"], "offset": offset})
                        if chunk.get("error"):
                            packet.clear()
                            break
                        packet.extend(base64.b64decode(chunk["data"], validate=True))
                    if len(packet) != state["bytes"]:
                        continue
                    message = {"op": "observation", "map_id": state["map_id"], "observation": json.loads(packet)}
                    finish_key = None
                else:
                    self.stop.wait(.05)
                    continue
                message['minimum_sequence']=state.get('sequence',0)
                self.process.stdin.write(json.dumps(message, separators=(",", ":"))+"\n")
                self.process.stdin.flush()
                busy = True
        except Exception:
            if not self.stop.is_set():
                self.ui.say("bridge_room_pc_failed")
        finally:
            try:
                self.exchange({"op": "offline"})
            except (OSError, ValueError, RuntimeError):
                pass
            if self.process and self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(3)
