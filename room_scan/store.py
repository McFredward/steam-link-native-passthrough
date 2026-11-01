"""Immutable room revisions and an atomically replaced current-revision pointer.

Drafts are kept separately and cannot be queried as published room data. This
store does not grant a tracking origin or implement Meta/OpenXR adapters.
"""
from __future__ import annotations

from contextlib import contextmanager
from dataclasses import replace
import json
import os
from pathlib import Path
import stat
import uuid

from .geometry import RoomError, require
from .model import Label, Room, identifier

MAX_BYTES = 16 * 1024 * 1024


def encode(value):
    result = (json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n").encode("utf-8")
    require(len(result) <= MAX_BYTES, "Room file budget exceeded")
    return result


def decode(raw):
    require(type(raw) is bytes and len(raw) <= MAX_BYTES, "Room file budget exceeded")
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "Duplicate JSON key")
            result[key] = value
        return result
    def constant(_):
        raise RoomError("Nonfinite JSON number")
    try:
        return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise RoomError("Invalid room JSON") from error


class RoomStore:
    def __init__(self, root):
        self.root = Path(root).absolute()
        # Explicit destination only. No default system/Steam directory is used.
        for p in (self.root, *self.root.parents):
            require(not p.is_symlink(), "Room storage cannot use symlinks")
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        info = self.root.stat()
        require(stat.S_ISDIR(info.st_mode), "Room storage is not a directory")
        if hasattr(os, "getuid"):
            require(info.st_uid == os.getuid() and info.st_mode & 0o077 == 0, "Private room storage required")

    def _read(self, name, limit=MAX_BYTES):
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        path = self.root / name
        require(not path.is_symlink(), "Room file cannot be a symlink")
        fd = os.open(path, flags)
        try:
            before = os.fstat(fd)
            require(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and before.st_size <= limit,
                    "Invalid room file")
            with os.fdopen(fd, "rb", closefd=False) as stream:
                raw = stream.read(limit+1)
            after = os.fstat(fd)
            require(len(raw) == before.st_size and (before.st_size, before.st_mtime_ns, before.st_ino)
                    == (after.st_size, after.st_mtime_ns, after.st_ino), "Room file changed while reading")
            return raw
        finally:
            os.close(fd)

    def _sync(self):
        if os.name == "posix":
            fd = os.open(self.root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                os.fsync(fd)
            finally:
                os.close(fd)

    def _write_new(self, name, raw):
        fd = os.open(self.root / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        self._sync()

    @contextmanager
    def _lock(self, room_id):
        name = room_id + ".lock"
        try:
            self._write_new(name, b"room-save-in-progress\n")
        except FileExistsError as error:
            raise RoomError("Another save is active; an interrupted save needs explicit recovery") from error
        try:
            yield
        finally:
            (self.root / name).unlink()
            self._sync()

    def _head(self, room_id):
        identifier(room_id)
        try:
            data = decode(self._read(room_id + ".head", 256))
        except FileNotFoundError:
            return 0
        require(type(data) is dict and set(data) == {"revision"} and type(data["revision"]) is int
                and 1 <= data["revision"] <= 1000000, "Invalid room revision pointer")
        return data["revision"]

    def load(self, room_id, *, revision=None):
        identifier(room_id)
        if revision is None:
            revision = self._head(room_id)
        require(type(revision) is int and 1 <= revision <= 1000000, "Room has no saved revision")
        room = Room.from_dict(decode(self._read(f"{room_id}.{revision}.json")))
        require(room.id == room_id and room.revision == revision, "Room revision identity differs")
        return room

    def save(self, room: Room, *, expected_revision: int):
        require(isinstance(room, Room) and type(expected_revision) is int and room.revision == expected_revision,
                "Room was edited from a different revision")
        require(room.partial_scan_accepted and room.mesh.triangles and all(e.confirmed for e in room.elements),
                "Only a reviewed measured room can be saved")
        saved = replace(room, revision=expected_revision+1)
        raw = encode(saved.to_dict())
        with self._lock(room.id):
            require(self._head(room.id) == expected_revision, "Saved room changed; reload before saving")
            # Immutable revisions precede the pointer. A failed pointer update
            # leaves the last committed room usable and preserves the candidate.
            self._write_new(f"{room.id}.{saved.revision}.json", raw)
            temporary = room.id + ".head." + uuid.uuid4().hex
            try:
                self._write_new(temporary, encode({"revision": saved.revision}))
                require(not (self.root / (room.id + ".head")).is_symlink(), "Foreign room pointer preserved")
                os.replace(self.root / temporary, self.root / (room.id + ".head"))
                self._sync()
            finally:
                (self.root / temporary).unlink(missing_ok=True)
        return saved

    def save_draft(self, session_id, room: Room):
        identifier(session_id)
        # Unique immutable draft checkpoints never replace a committed head.
        name = f"draft.{session_id}.{uuid.uuid4()}.json"
        self._write_new(name, encode({"schema": "sfnp-room-draft-v1", "room": room.to_dict()}))
        return name

    def query(self, room_id, label: Label, *, current_origin_id: str):
        identifier(current_origin_id)
        room = self.load(room_id)
        require(not room.test_only, "Test capture cannot be used as a real room")
        require(room.partial_scan_accepted and room.mesh.triangles and all(e.confirmed for e in room.elements),
                "Saved room is not ready for game queries")
        require(room.origin_id == current_origin_id, "Saved room must be relocalized before use")
        return room.query(label)
