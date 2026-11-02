#!/usr/bin/env python3
"""Client for the manually started, same-user diagnostic camera API.

This is intentionally outside the normal installer. Image interpretation,
eye identity and an exact exposure pose remain explicit unverified fields.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import socket
import struct
import time


class Client:
    def __init__(self, address):
        if not re.fullmatch(r"sfnp\.room\.camera\."+str(os.getuid())+r"\.[0-9a-f]{32}", address):
            raise ValueError("Unexpected camera session address")
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.socket.settimeout(3)
        self.socket.connect("\0"+address)
        peer = struct.unpack("3i", self.socket.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if peer[1] != os.getuid():
            self.close()
            raise ValueError("Camera API peer has another UID")

    def request(self, command, **values):
        data = json.dumps(dict(command=command, **values), separators=(",", ":")).encode()
        if len(data) > 512:
            raise ValueError("Camera request exceeds budget")
        if self.socket.send(data) != len(data):
            raise OSError("Incomplete camera request")
        packet, _, flags, _ = self.socket.recvmsg(400400)
        if len(packet) < 8 or flags & socket.MSG_TRUNC:
            raise ValueError("Camera response extent invalid")
        metadata_bytes, image_bytes = struct.unpack_from("<II", packet)
        if metadata_bytes > 16384 or image_bytes > 384000 or len(packet) != 8+metadata_bytes+image_bytes:
            raise ValueError("Camera response size invalid")
        metadata = json.loads(packet[8:8+metadata_bytes])
        pixels = packet[8+metadata_bytes:]
        if pixels:
            if metadata.get("schema") != "sfnp-native-camera-frame-v1" or metadata.get("room_scan_ready") is not False:
                raise ValueError("Unexpected diagnostic frame contract")
            width, height = metadata.get("width"), metadata.get("height")
            if type(width) is not int or type(height) is not int or width <= 0 or height <= 0 or width*height != len(pixels):
                raise ValueError("Camera image geometry invalid")
        return metadata, pixels

    def close(self):
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address")
    parser.add_argument("--stop", action="store_true")
    parser.add_argument("--frames", type=int, default=0)
    parser.add_argument("--save-first", type=Path)
    args = parser.parse_args()
    if not 0 <= args.frames <= 100:
        parser.error("Frame count must be 0..100")
    client = Client(args.address)
    try:
        if args.stop:
            print(json.dumps(client.request("stop")[0]))
            return
        description = client.request("describe")[0]
        print(json.dumps(description), flush=True)
        sensors = [item["sensor"] for item in description.get("streams", [])]
        if args.frames and (len(sensors) != 2 or len(set(sensors)) != 2):
            raise ValueError("Expected two distinct Arcturus sources")
        counts, sequences, saved = dict.fromkeys(sensors, 0), {}, set()
        while counts and min(counts.values()) < args.frames:
            for sensor in sensors:
                metadata, pixels = client.request("latest", sensor=sensor)
                if not pixels or metadata.get("sequence") == sequences.get(sensor):
                    continue
                sequences[sensor] = metadata["sequence"]
                counts[sensor] += 1
                print(json.dumps(metadata), flush=True)
                if args.save_first and sensor not in saved:
                    args.save_first.mkdir(mode=0o700, parents=True, exist_ok=True)
                    name = re.sub(r"[^a-zA-Z0-9_-]", "_", sensor)+".pgm"
                    path = args.save_first/name
                    with path.open("xb") as output:
                        output.write(f"P5\n{metadata['width']} {metadata['height']}\n255\n".encode()+pixels)
                    path.chmod(0o600)
                    saved.add(sensor)
            time.sleep(.05)
    finally:
        client.close()


if __name__ == "__main__":
    main()
