#!/usr/bin/env python3
"""Manually start the diagnostic camera API on the Frame; no installation.

The root launcher validates the supplied SHA256, seals the exact bytes in RAM,
then executes them. The native helper enforces its read-only syscall policy
and drops setup privileges before exposing its ephemeral same-user API.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

ROOT_LAUNCHER = r'''
import os,sys,hashlib,fcntl,stat
path,expected,mode=sys.argv[1:]
if mode not in ('--probe-buffers','--serve'):raise SystemExit('Invalid camera mode')
uid=int(os.environ['SUDO_UID']);gid=int(os.environ['SUDO_GID'])
fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC);info=os.fstat(fd)
if not stat.S_ISREG(info.st_mode) or info.st_uid!=uid or not 64<info.st_size<=5*1024*1024 or info.st_mode&0o022:
 raise SystemExit('Untrusted prototype file')
with os.fdopen(fd,'rb') as file:raw=file.read(5*1024*1024+1)
if hashlib.sha256(raw).hexdigest()!=expected:raise SystemExit('Prototype bytes differ from the requested SHA256')
memory=os.memfd_create('sfnp-readonly-camera',os.MFD_ALLOW_SEALING)
with os.fdopen(os.dup(memory),'wb') as file:file.write(raw)
fcntl.fcntl(memory,fcntl.F_ADD_SEALS,fcntl.F_SEAL_WRITE|fcntl.F_SEAL_GROW|fcntl.F_SEAL_SHRINK|fcntl.F_SEAL_SEAL)
os.execve(memory,['sfnp-room-camera-native',mode],{'SUDO_UID':str(uid),'SUDO_GID':str(gid),'LANG':'C.UTF-8'})
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--probe-buffers", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch("[0-9a-f]{64}", args.sha256):
        parser.error("Expected a complete lowercase SHA256")
    path = args.binary.resolve(strict=True)
    if hashlib.sha256(path.read_bytes()).hexdigest() != args.sha256:
        parser.error("Prototype does not match the supplied SHA256")
    mode = "--probe-buffers" if args.probe_buffers else "--serve"
    child = subprocess.Popen(["sudo", "--", "/usr/bin/python3", "-I", "-B", "-c", ROOT_LAUNCHER,
                              str(path), args.sha256, mode])
    try:
        return child.wait()
    except KeyboardInterrupt:
        # Signal only the owned sudo monitor; it forwards termination to this
        # helper. The helper also stops if its session parent disappears.
        if child.poll() is None:
            child.terminate()
        child.wait()
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
