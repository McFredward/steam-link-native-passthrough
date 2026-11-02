"""Manually arm the read-only camera helper until Stop, owner exit or reboot.

This installs no unit, hook, capabilities, driver, firmware or boot setting.
Passwords are accepted through memory/stdin only. The detached owner is an
ordinary user process; the native reader permanently drops setup privileges.
"""
from __future__ import annotations

import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

SCHEMA = "sfnp-room-camera-session-v1"
MAX_SOURCE = 65536
ROOT_LAUNCHER = r'''
import os,sys,hashlib,fcntl,stat
path,expected,mode,owner,started=sys.argv[1:]
if mode!='--serve' or not owner.isdecimal() or not started.isdecimal():raise SystemExit('Invalid manual camera mode')
uid=int(os.environ['SUDO_UID']);gid=int(os.environ['SUDO_GID'])
process='/proc/'+owner
if os.stat(process).st_uid!=uid:raise SystemExit('Camera session owner changed')
with open(process+'/stat') as file:record=file.read(8193)
if len(record)>8192 or record[record.rfind(')')+1:].split()[19]!=started:raise SystemExit('Camera session owner changed')
fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC);info=os.fstat(fd)
if not stat.S_ISREG(info.st_mode) or info.st_uid!=uid or not 64<info.st_size<=5*1024*1024 or info.st_mode&0o022:
 raise SystemExit('Untrusted camera helper file')
with os.fdopen(fd,'rb') as file:raw=file.read(5*1024*1024+1)
if hashlib.sha256(raw).hexdigest()!=expected:raise SystemExit('Camera helper differs from the checked package')
memory=os.memfd_create('sfnp-readonly-camera',os.MFD_ALLOW_SEALING)
with os.fdopen(os.dup(memory),'wb') as file:file.write(raw)
fcntl.fcntl(memory,fcntl.F_ADD_SEALS,fcntl.F_SEAL_WRITE|fcntl.F_SEAL_GROW|fcntl.F_SEAL_SHRINK|fcntl.F_SEAL_SEAL)
os.execve(memory,['sfnp-room-camera-native',mode],{'SUDO_UID':str(uid),'SUDO_GID':str(gid),'LANG':'C.UTF-8',
 'SFNP_ROOM_OWNER_PID':owner,'SFNP_ROOM_OWNER_START':started})
'''


def _directory(root=None, *, create=False):
    path = Path(root) if root is not None else Path("/run/user")/str(os.getuid())/"sfnp-room-camera"
    if not path.is_absolute() or os.getuid() == 0:
        raise ValueError("A private user camera-session directory is required")
    if create:
        try:
            path.mkdir(mode=0o700)
        except FileExistsError:
            pass
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise ValueError("The camera-session directory is not private")
    return path


def _record(root=None):
    try:
        path = _directory(root)/"session.json"
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    except FileNotFoundError:
        return None
    with os.fdopen(fd, "rb") as file:
        info = os.fstat(file.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077 or not 0 < info.st_size <= 4096:
            raise ValueError("The camera-session descriptor is not private")
        data = json.loads(file.read(4097))
    if (type(data) is not dict or data.get("schema") != SCHEMA or data.get("state") != "armed"
            or not re.fullmatch(r"sfnp\.room\.camera\."+str(os.getuid())+r"\.[0-9a-f]{32}", data.get("abstract_socket", ""))
            or type(data.get("helper_pid")) is not int or not 1 < data["helper_pid"] < 2**31
            or not re.fullmatch("[0-9a-f]{64}", data.get("helper_sha256", ""))):
        raise ValueError("The camera-session descriptor is invalid")
    return data


def _request(address, command, *, expected_pid=None):
    if command not in ("describe", "stop") or not re.fullmatch(
            r"sfnp\.room\.camera\."+str(os.getuid())+r"\.[0-9a-f]{32}", address):
        raise ValueError("Invalid camera-session request")
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as peer:
        peer.settimeout(3)
        peer.connect("\0"+address)
        identity = struct.unpack("3i", peer.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if identity[1] != os.getuid() or expected_pid is not None and identity[0] != expected_pid:
            raise ValueError("The camera session belongs to another process")
        request = json.dumps({"command": command}, separators=(",", ":")).encode()
        if peer.send(request) != len(request):
            raise OSError("Incomplete camera-session request")
        packet, _, flags, _ = peer.recvmsg(16392)
        if len(packet) < 8 or flags & socket.MSG_TRUNC:
            raise ValueError("Invalid camera-session response")
        count, images = struct.unpack_from("<II", packet)
        if count > 16384 or images or len(packet) != 8+count:
            raise ValueError("Unexpected camera-session response extent")
        return json.loads(packet[8:]), identity[0]


def status(root=None):
    data = _record(root)
    if data is None:
        return {"state": "inactive"}
    try:
        report, _ = _request(data["abstract_socket"], "describe", expected_pid=data["helper_pid"])
    except (FileNotFoundError, ConnectionRefusedError):
        return {"state": "inactive"}
    if (report.get("schema") != "sfnp-native-camera-api-v1" or report.get("reader_uid") != os.getuid()
            or report.get("setup_privileges_dropped") is not True or report.get("manual_owner_monitored") is not True
            or report.get("maps_read_only") is not True or report.get("boot_changes") is not False
            or report.get("camera_settings_changed") is not False):
        raise ValueError("The camera-session safety contract could not be verified")
    return {"state": "armed", "helper_sha256": data["helper_sha256"],
            "pixel_reads_active": report.get("pixel_reads_active") is True}


def stop_session(root=None):
    data = _record(root)
    if data is None:
        return {"state": "inactive"}
    try:
        reply, _ = _request(data["abstract_socket"], "stop", expected_pid=data["helper_pid"])
    except (FileNotFoundError, ConnectionRefusedError):
        return {"state": "inactive"}
    if reply.get("status") != "stopping":
        raise ValueError("The camera reader did not confirm Stop")
    deadline = time.monotonic()+5
    while time.monotonic() < deadline:
        current = _record(root)
        if current is None or current["abstract_socket"] != data["abstract_socket"]:
            return {"state": "inactive"}
        time.sleep(.025)
    raise ValueError("Camera Stop was requested; its session cleanup is still pending")


def start_session(binary, expected, password, owner_source, *, root=None):
    if root is not None:
        # The production owner always uses /run/user/UID. Test roots exercise
        # descriptor ownership separately; they cannot launch a privileged reader.
        raise ValueError("A manual camera owner requires the native user runtime directory")
    if type(password) is not str or len(password.encode()) > 1023 or any(c in password for c in "\r\n\0"):
        raise ValueError("Invalid camera-session password input")
    if type(owner_source) is not bytes or not 0 < len(owner_source) <= MAX_SOURCE or not re.fullmatch("[0-9a-f]{64}", expected):
        raise ValueError("Checked camera-session source and helper digest are required")
    path = Path(binary)
    info = path.lstat()
    if (not path.is_absolute() or not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
            or info.st_mode & 0o022 or not 64 < info.st_size <= 5*1024*1024
            or hashlib.sha256(path.read_bytes()).hexdigest() != expected):
        raise ValueError("The camera helper differs from the checked installation")
    current = status()
    if current["state"] == "armed":
        if current["helper_sha256"] == expected:
            return current
        stop_session()
    process = subprocess.Popen([sys.executable, "-I", "-B", "-c", owner_source.decode("utf-8"),
        "--owner", str(path), expected], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, start_new_session=True, close_fds=True)
    try:
        process.stdin.write(password.encode()+b"\n")
        process.stdin.close()
        password = ""
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            if not selector.select(20):
                raise ValueError("Manual camera setup did not complete")
        reply = process.stdout.readline(4097)
        if not 0 < len(reply) <= 4096:
            raise ValueError("Manual camera setup did not return a bounded status")
        result = json.loads(reply)
        if result.get("state") != "armed":
            raise ValueError("Manual camera access could not start. Check the Frame password and keep SteamVR running.")
        checked = status()
        if checked["state"] != "armed" or checked["helper_sha256"] != expected:
            raise ValueError("The manual camera session could not be verified")
        return checked
    except BaseException:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        raise
    finally:
        password = ""
        process.stdout.close()


def _owner_main(binary, expected):
    stopped = False
    def terminate(*_):
        nonlocal stopped
        stopped = True
        raise InterruptedError("Manual camera session stopped")
    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGHUP, terminate)
    signal.signal(signal.SIGINT, terminate)
    child = None
    descriptor = None
    root = _directory(create=True)
    lock_fd = os.open(root/"owner.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
    with os.fdopen(lock_fd, "rb") as lock:
        info = os.fstat(lock.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError("The camera-session lock is not private")
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        _record()  # Preserve an unknown or foreign descriptor, even when stale.
        try:
            password = sys.stdin.buffer.readline(1025)
            if not password.endswith(b"\n") or len(password) > 1024:
                raise ValueError("Invalid camera-session sign-in input")
            process = Path("/proc")/str(os.getpid())/"stat"
            record = process.read_text()
            started = record[record.rfind(")")+1:].split()[19]
            child = subprocess.Popen(["/usr/bin/sudo", "-S", "-p", "", "--", "/usr/bin/python3", "-I", "-B", "-c",
                ROOT_LAUNCHER, binary, expected, "--serve", str(os.getpid()), started], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
            child.stdin.write(password)
            child.stdin.close()
            password = b""
            with selectors.DefaultSelector() as selector:
                selector.register(child.stdout, selectors.EVENT_READ)
                if not selector.select(15):
                    raise ValueError("Manual reader setup timed out")
            announcement = child.stdout.readline(16385)
            if not 0 < len(announcement) <= 16384:
                raise ValueError("Manual reader startup failed")
            native = json.loads(announcement)
            address = native["abstract_socket"]
            report, peer_pid = _request(address, "describe")
            if (report.get("reader_uid") != os.getuid() or report.get("setup_privileges_dropped") is not True
                    or report.get("manual_owner_monitored") is not True or report.get("maps_read_only") is not True
                    or report.get("boot_changes") is not False or report.get("camera_settings_changed") is not False):
                raise ValueError("Manual reader safety contract invalid")
            descriptor = {"schema": SCHEMA, "state": "armed", "abstract_socket": address,
                "helper_pid": peer_pid, "helper_sha256": expected, "owner_pid": os.getpid()}
            fd, temporary = tempfile.mkstemp(prefix="session.", suffix=".tmp", dir=root)
            try:
                with os.fdopen(fd, "wb") as file:
                    file.write((json.dumps(descriptor, separators=(",", ":"))+"\n").encode())
                os.replace(temporary, root/"session.json")
            finally:
                Path(temporary).unlink(missing_ok=True)
            print(json.dumps({"state": "armed"}), flush=True)
            # The installer can now exit. Only this ordinary user owner remains.
            os.close(1)
            while not stopped and child.poll() is None:
                time.sleep(.1)
        finally:
            password = b""
            if child is not None and child.poll() is None:
                if descriptor is not None:
                    try:
                        _request(descriptor["abstract_socket"], "stop", expected_pid=descriptor["helper_pid"])
                    except (OSError, ValueError):
                        pass
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    # Native pidfd monitoring also ends the reader if this
                    # owner exits, independent of sudo monitor parentage.
                    try:
                        child.terminate()
                    except (ProcessLookupError, PermissionError):
                        pass
            if descriptor is not None:
                current = _record()
                if current and current["abstract_socket"] == descriptor["abstract_socket"]:
                    (root/"session.json").unlink()


if __name__ == "__main__":
    try:
        if len(sys.argv) != 4 or sys.argv[1] != "--owner":
            raise ValueError("Use the installer's explicit Enable room capture action")
        _owner_main(sys.argv[2], sys.argv[3])
    except Exception:
        try:
            print(json.dumps({"state": "inactive", "reason": "camera-access-unavailable"}), flush=True)
        except OSError:
            pass
        raise SystemExit(1)
