"""Owned GPU subprocess. No network sockets, credentials or Frame service control."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--width", type=int, default=512)
    parser.add_argument("--height", type=int, default=384)
    parser.add_argument("--limit", type=int, default=60000)
    args = parser.parse_args()
    sys.path.insert(0, str(args.runtime/"vendor"))
    dll_directories = []
    if os.name == "nt":
        paths = sorted({dll.parent for dll in (args.runtime/"vendor").rglob("*.dll")})
        # cuDNN's runtime engines use their own LoadLibrary calls. Keep this
        # search path private to the owned GPU process; never edit system PATH.
        os.environ["PATH"] = os.pathsep.join([*(str(path) for path in paths), os.environ.get("PATH", "")])
        for path in paths:
            dll_directories.append(os.add_dll_directory(str(path)))
    from .live_inference import StereoInference
    from .live_geometry import rectify, triangulate
    from .live_map import GaussianMap
    def emit(value):
        sys.stdout.write(json.dumps(value, separators=(",", ":"), allow_nan=False)+"\n")
        sys.stdout.flush()
    try:
        engine = StereoInference(args.runtime, args.width, args.height)
        engine.warmup()
        emit({"event": "ready", "provider": engine.provider, "protocol": 2,
              "width": args.width, "height": args.height})
        model = GaussianMap(args.limit)
        map_id, binding, epoch, observation_sequence, exposure, serial = None, None, None, 0, 0, 0
        for raw in sys.stdin:
            if len(raw) > 5200000:
                raise ValueError("PC worker input exceeds budget")
            message = json.loads(raw)
            if message["map_id"] != map_id:
                model, map_id, serial = GaussianMap(args.limit), message["map_id"], 0
                binding = None
            serial = max(serial, message.get("minimum_sequence", 0))
            if message["op"] == "seed":
                model.restore(message["gaussians"])
                binding, epoch = message["binding"], message["epoch"]
                emit({"event": "seeded"})
                continue
            if message["op"] == "finish":
                if binding and any(binding[k] != message["binding"][k] for k in ("origin_id", "calibration_id")):
                    raise ValueError("Changed room origin at polygon extraction")
                binding, epoch = message["binding"], message["epoch"]
                serial += 1
                emit({"event": "pc_mesh", "binding": binding, "capture_epoch": epoch,
                      "sequence": serial, "mesh": model.mesh()})
                continue
            observation = message["observation"]
            if observation["binding"] != binding:
                if binding and any(binding[k] != observation["binding"][k] for k in ("origin_id", "calibration_id")):
                    raise ValueError("Room origin changed during reconstruction")
                binding, epoch = observation["binding"], observation["capture_epoch"]
                observation_sequence = exposure = 0
            if observation["sequence"] <= observation_sequence or observation["exposure_ticks"] <= exposure:
                raise ValueError("Stale stereo observation")
            observation_sequence, exposure = observation["sequence"], observation["exposure_ticks"]
            started = time.perf_counter()
            images, masks, origins, rays, room, intrinsics = rectify(observation, args.width, args.height)
            disparities = engine.disparity(images)
            points, normals, valid, depth = triangulate(disparities, masks, origins, rays, room, intrinsics)
            model.integrate(points, normals, valid, depth, room[:, 3], intrinsics[0], room)
            serial += 1
            if not model.active.any():
                emit({"event": "pc_hint", "binding": binding, "capture_epoch": epoch, "sequence": serial})
                continue
            emit({"event": "pc_model", "binding": binding, "capture_epoch": epoch, "sequence": serial,
                  **model.snapshot(), "at_capacity": model.capacity_limited,
                  "compute_ms": round((time.perf_counter()-started)*1000, 2), "accepted_pixels": int(valid.sum())})
    except Exception as error:
        print("PC room reconstruction: "+type(error).__name__+": "+str(error)[:240], file=sys.stderr)
        emit({"event": "error", "code": "pc-reconstruction-failed"})
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
