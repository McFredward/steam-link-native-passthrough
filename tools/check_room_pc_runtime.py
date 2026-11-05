#!/usr/bin/env python3
"""Check the pinned Windows inference runtime without a headset or camera access."""
from __future__ import annotations

import argparse
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "win32" or sys.version_info[:2] != (3, 14):
        raise RuntimeError("This check requires Windows Python 3.14")
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("room_setup_check", root/"setup/steam_link_setup_room.py")
    setup = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(setup)
    # Match the actual installer's adjacent model notices.
    inputs = args.cache/"input"
    inputs.mkdir(parents=True, exist_ok=True)
    (inputs/"room-pc-runtime.json").write_bytes((root/"packaging/room-pc-runtime.json").read_bytes())
    notices = inputs/"room-licenses"
    notices.mkdir(exist_ok=True)
    for name in json.loads((inputs/"room-pc-runtime.json").read_text())["licenses"]:
        (notices/name).write_bytes((root/"room_scan/licenses"/name).read_bytes())
    runtime = setup.prepare_runtime(inputs/"room-pc-runtime.json", args.cache)
    paths = sorted({dll.parent for dll in (runtime/"vendor").rglob("*.dll")})
    os.environ["PATH"] = os.pathsep.join([*(str(path) for path in paths), os.environ.get("PATH", "")])
    directories = [os.add_dll_directory(str(path)) for path in paths]
    sys.path.insert(0, str(root))
    sys.path.insert(0, str(runtime/"vendor"))
    from room_reconstruction.live_inference import StereoInference
    engine = StereoInference(runtime, 320, 256, cpu_development=True,batch=2)
    engine.warmup()
    import pyfqmr
    import numpy as np
    reducer=pyfqmr.Simplify()
    reducer.setMesh(np.array(((0.,0.,0.),(1.,0.,0.),(0.,1.,0.))),np.array(((0,1,2),)))
    reducer.simplify_mesh(target_count=1,preserve_border=True,verbose=False)
    assert len(reducer.getMesh()[1])==1
    import shapely
    measured=shapely.Polygon(((0,0),(3,0),(3,3),(0,3)),holes=[((1,1),(2,1),(2,2),(1,2))])
    triangles=shapely.constrained_delaunay_triangles(measured)
    assert shapely.equals(shapely.union_all(shapely.get_parts(triangles)),measured)
    provider = runtime/"vendor/onnxruntime/capi/onnxruntime_providers_cuda.dll"
    loaded = ctypes.WinDLL(str(provider))
    print(json.dumps({"result": "PASS", "cuda_dll_loaded": bool(loaded), "private_dll_directories": len(directories),
                      "actual_onnx_inference": True, "bilateral_dynamic_batch":2,"native_mesh_simplifier": True,
                      "measured_polygon_union_and_real_opening": True, "provider": engine.provider,
                      "nvidia_gpu_performance_verified": False, "headset_test": False}))


if __name__ == "__main__":
    main()
