"""Owned GPU subprocess. No network sockets, credentials or Frame service control."""
from __future__ import annotations

import argparse
import hashlib
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
    parser.add_argument('--maps',type=Path)
    parser.add_argument('--batch',type=int,default=1)
    parser.add_argument('--vram-mb',type=int,default=3072)
    args = parser.parse_args()
    for name in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS'):
        os.environ[name]='4'
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
    from .live_tiles import TiledGaussianMap
    from .live_coverage import DirectionCoverage
    from .live_refine import refine_stereo
    import pyfqmr  # Required and verified before advertising readiness.
    import shapely  # Includes the private GEOS union and constrained triangulator.
    if not callable(shapely.constrained_delaunay_triangles):
        raise RuntimeError('The measured-surface triangulator is unavailable')
    def emit(value):
        sys.stdout.write(json.dumps(value, separators=(",", ":"), allow_nan=False)+"\n")
        sys.stdout.flush()
    try:
        probe=shapely.Polygon(((0,0),(3,0),(3,3),(0,3)),holes=[((1,1),(2,1),(2,2),(1,2))])
        checked=shapely.union_all(shapely.get_parts(shapely.constrained_delaunay_triangles(probe)))
        if not shapely.equals(probe,checked):
            raise RuntimeError('The measured-surface runtime did not preserve its opening')
        engine = StereoInference(args.runtime, args.width, args.height,batch=args.batch,vram_mb=args.vram_mb)
        engine.warmup()
        emit({"event": "ready", "provider": engine.provider, "protocol": 4,'tiled':True,
              "width": args.width, "height": args.height})
        model = None
        map_id, binding, epoch, observation_sequence, exposure, serial = None, None, None, 0, 0, 0
        for raw in sys.stdin:
            if len(raw) > 6500000:
                raise ValueError("PC worker input exceeds budget")
            message = json.loads(raw)
            if message['op']=='flush':
                if model is not None:model.flush()
                emit({'event':'seeded'})
                continue
            if message["map_id"] != map_id:
                if model is not None:model.flush()
                map_id,serial=message['map_id'],0
                identity=message.get('binding') or message['observation']['binding']
                token=hashlib.sha256(json.dumps([map_id,identity['origin_id'],identity['calibration_id']]).encode()).hexdigest()
                model=TiledGaussianMap((args.maps or args.runtime.parent/'room-maps')/token)
                coverage=DirectionCoverage(model.coverage)
                binding = None
            serial = max(serial, message.get("minimum_sequence", 0))
            if message["op"] == "seed":
                if 'surface' in message:
                    import base64
                    if not model.index:model.seed(base64.b64decode(message['surface'],validate=True))
                else:
                    raise ValueError('A measured connected surface is required to resume this room')
                binding, epoch = message["binding"], message["epoch"]
                model.dirty.update(model.index)
                observation_sequence=exposure=0
                emit({"event": "seeded"})
                continue
            if message["op"] == "finish":
                if binding and any(binding[k] != message["binding"][k] for k in ("origin_id", "calibration_id")):
                    raise ValueError("Changed room origin at polygon extraction")
                binding, epoch = message["binding"], message["epoch"]
                while model.dirty or model.pending:
                    snapshot=model.snapshot();serial+=1
                    emit({'event':'pc_model','binding':binding,'capture_epoch':epoch,
                          'sequence':serial,**snapshot,'coverage':model.coverage})
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
            optical_support=masks[0]&masks[1]
            rectified_at=time.perf_counter()
            disparities = engine.disparity(images)
            stereo_at=time.perf_counter()
            depth_stats={'optical':int(optical_support.sum())}
            disparities,masks=refine_stereo(images,disparities,masks)
            depth_stats['photometric']=int(masks[0].sum())
            points, normals, valid, depth = triangulate(disparities, masks, origins, rays, room, intrinsics,
                                                        lr_tolerance=.35,diagnostics=depth_stats)
            model.coverage=coverage.update(rays,optical_support,valid)
            geometry_at=time.perf_counter()
            model.integrate(points, normals, valid, depth, room[:, 3], intrinsics[0], room)
            fused_at=time.perf_counter()
            serial += 1
            snapshot=model.snapshot() if model.index else {};previewed_at=time.perf_counter()
            phases=dict(zip(("rectify","stereo","geometry","fusion","preview"),
                [round((b-a)*1000,2) for a,b in zip((started,rectified_at,stereo_at,geometry_at,fused_at),
                                                               (rectified_at,stereo_at,geometry_at,fused_at,previewed_at))]))
            emit({"event": "pc_model" if model.index else 'pc_hint', "binding": binding, "capture_epoch": epoch, "sequence": serial,
                  **snapshot, "phase_ms":phases,
                  'coverage':model.coverage,'depth_stats':depth_stats,
                  "compute_ms": round((time.perf_counter()-started)*1000, 2), "accepted_pixels": int(valid.sum())})
    except Exception as error:
        print("PC room reconstruction: "+type(error).__name__+": "+str(error)[:240], file=sys.stderr)
        emit({"event": "error", "code": "pc-reconstruction-failed"})
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
