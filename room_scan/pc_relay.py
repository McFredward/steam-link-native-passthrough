"""Private same-user relay. Camera packets and model replies are latest-only."""
from __future__ import annotations

import base64
import hashlib
import json
import os
from pathlib import Path
import queue
import secrets
import socket
import struct
import threading
import time
import zlib

from .geometry import require

CHUNK = 60000
MAX_MODEL = 12 * 1024 * 1024
PROTOCOL = 2


def unpack(raw, expanded, limit):
    require(type(expanded) is int and 0 < expanded <= limit, "Invalid expanded room packet size")
    decoder = zlib.decompressobj()
    value = decoder.decompress(raw, expanded+1)
    require(len(value) == expanded and decoder.eof and not decoder.unused_data
            and not decoder.unconsumed_tail, "Compressed room packet exceeds its checked extent")
    return value


class PcRelay:
    def __init__(self, root=None):
        runtime = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
        self.root = Path(root) if root else Path(runtime)/"sfnp-room-pc"
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        info = self.root.lstat()
        require(self.root.is_dir() and not self.root.is_symlink() and info.st_uid == os.getuid()
                and not info.st_mode & 0o077, "Private room relay directory required")
        self.address = "sfnp-room-pc-" + secrets.token_hex(16)
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.socket.bind("\0" + self.address)
        self.socket.listen(2)
        self.socket.settimeout(.1)
        self.lock = threading.Lock()
        self.events = queue.Queue(maxsize=2)
        self.pending = self.lease = None
        self.upload = None
        self.counter = 0
        self.ready_until = 0.
        self.binding = None
        self.epoch = 0
        self.intent = "idle"
        self.map_id = None
        self.model_path = None
        self.seed = None
        self.sequence = 0
        self.stopping = threading.Event()
        record = {"schema": "sfnp-room-pc-session-v1", "pid": os.getpid(), "socket": self.address}
        descriptor = self.root / "session.json"
        fd = os.open(descriptor, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600) if not descriptor.exists() else None
        if fd is None:
            old = json.loads(descriptor.read_text())
            try:
                os.kill(old["pid"], 0)
            except ProcessLookupError:
                descriptor.unlink()
                fd = os.open(descriptor, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
            else:
                raise RuntimeError("Room relay already running")
        with os.fdopen(fd, "w") as stream:
            json.dump(record, stream)
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    @property
    def ready(self):
        return time.monotonic() < self.ready_until

    def set_scan(self, binding, epoch, intent, map_id=None, model_path=None, sequence=0):
        with self.lock:
            if (binding, epoch) != (self.binding, self.epoch):
                self.pending = self.lease = self.upload = None
                while not self.events.empty():
                    self.events.get_nowait()
            self.binding, self.epoch, self.intent = binding, epoch, intent
            self.map_id = map_id
            self.model_path, self.sequence = model_path, sequence
            if intent == "idle":
                self.pending = self.lease = self.upload = None

    def offer(self, observation):
        raw = json.dumps(observation, separators=(",", ":"), allow_nan=False).encode()
        require(len(raw) < 1100000, "Camera packet exceeds relay budget")
        with self.lock:
            if (self.ready and self.intent == "scan" and observation["binding"] == self.binding
                    and observation["capture_epoch"] == self.epoch):
                self.counter += 1
                self.pending = self.counter, zlib.compress(raw,1), len(raw)

    def _dispatch(self, request):
        with self.lock:
            command = request["op"]
            if command == "offline":
                self.ready_until = 0
                self.pending = self.lease = self.upload = None
                return {"ok": True}
            if command == "ready":
                require(request.get("protocol") == PROTOCOL and request.get("provider") == "CUDAExecutionProvider",
                        "A warmed CUDA worker is required")
                self.ready_until = time.monotonic() + 15
                return {"ok": True}
            require(self.ready, "PC reconstruction is not ready")
            self.ready_until = time.monotonic() + 15
            state = {"binding": self.binding, "epoch": self.epoch, "intent": self.intent, "map_id": self.map_id, "sequence": self.sequence}
            if command == "take":
                self.lease, self.pending = self.pending, None
                if request.get("seed"):
                    self.seed = self.model_path.read_bytes() if self.model_path else None
                return {**state, "id": self.lease[0] if self.lease else None,
                        "bytes": len(self.lease[1]) if self.lease else 0,
                        "encoding":"zlib", "expanded_bytes":self.lease[2] if self.lease else 0,
                        "seed_bytes": len(self.seed) if self.seed else 0}
            if command == "seed_chunk":
                offset = request["offset"]
                require(self.seed and type(offset) is int and 0 <= offset < len(self.seed), "Invalid Gaussian seed chunk")
                return {"data": base64.b64encode(self.seed[offset:offset+CHUNK]).decode()}
            if command == "chunk":
                require(self.lease and request["id"] == self.lease[0], "Camera lease changed")
                offset = request["offset"]
                require(type(offset) is int and 0 <= offset < len(self.lease[1]), "Invalid camera chunk")
                return {"data": base64.b64encode(self.lease[1][offset:offset+CHUNK]).decode()}
            if command == "begin":
                require(request["binding"] == self.binding and request["epoch"] == self.epoch and self.intent != "idle",
                        "Scan ended or capture binding changed")
                size, digest = request["bytes"], request["sha256"]
                require(type(size) is int and 0 < size <= MAX_MODEL and type(digest) is str and len(digest) == 64,
                        "Invalid model transfer")
                require(request.get("encoding") == "zlib" and type(request.get("expanded_bytes")) is int
                        and 0 < request['expanded_bytes'] <= MAX_MODEL, "Invalid room packet encoding")
                self.upload = (size, digest, bytearray(), request['expanded_bytes'])
                return {"ok": True}
            if command == "put":
                require(self.upload and request["offset"] == len(self.upload[2]), "Model transfer is out of order")
                raw = base64.b64decode(request["data"], validate=True)
                require(0 < len(raw) <= CHUNK and len(self.upload[2])+len(raw) <= self.upload[0], "Model chunk exceeds budget")
                self.upload[2].extend(raw)
                return {"ok": True}
            if command == "commit":
                require(self.upload, "No model transfer")
                size, digest, raw, expanded = self.upload
                self.upload = None
                require(len(raw) == size and hashlib.sha256(raw).hexdigest() == digest, "Model transfer changed")
                value = json.loads(unpack(raw,expanded,MAX_MODEL))
                require(value["binding"] == self.binding and value["capture_epoch"] == self.epoch
                        and self.intent != "idle", "Late result refused")
                if self.events.full():
                    self.events.get_nowait()
                self.events.put_nowait(value)
                return {"ok": True}
            raise ValueError("Unknown room relay operation")

    def _serve(self):
        while not self.stopping.is_set():
            try:
                connection, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with connection:
                connection.settimeout(2)
                try:
                    _, uid, _ = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
                    require(uid == os.getuid(), "Room relay peer mismatch")
                    raw, _, flags, _ = connection.recvmsg(96000)
                    require(not flags & socket.MSG_TRUNC, "Room relay request exceeds budget")
                    reply = self._dispatch(json.loads(raw))
                except (OSError, ValueError, KeyError, TypeError):
                    reply = {"error": "Room relay request refused"}
                try:
                    connection.sendall(json.dumps(reply, separators=(",", ":"), allow_nan=False).encode())
                except OSError:
                    pass

    def close(self):
        self.stopping.set()
        self.socket.close()
        self.thread.join(3)
        descriptor = self.root / "session.json"
        if descriptor.exists() and json.loads(descriptor.read_text()).get("pid") == os.getpid():
            descriptor.unlink()
