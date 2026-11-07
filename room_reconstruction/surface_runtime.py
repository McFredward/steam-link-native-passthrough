"""Isolated private Windows Gaussian runtime; no Frame or camera operations."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time


def warmup_command(runtime, manifest, *, cpu_development=False):
    """Start from the standard library, without inherited ORT/Python DLL paths.

    This describes a new child; it never imports either numerical runtime in
    the installer. The caller owns the process, cancellation and its timeout.
    CPU development is explicit and must never grant GPU scanner readiness.
    """
    root = Path(__file__).resolve().parents[1]
    bootstrap = ('import sys;sys.path.insert(0,sys.argv.pop(1));'
                 'from room_reconstruction.surface_runtime import main;main()')
    command = [sys.executable, '-I', '-S', '-B', '-c', bootstrap, str(root),
               '--runtime', str(Path(runtime).resolve()), '--manifest', str(Path(manifest).resolve())]
    if cpu_development:
        command.append('--cpu-development')
    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith(('PYTHON', 'CUDA_PATH', 'CUDA_HOME'))}
    paths = [str(Path(sys.executable).resolve().parent), sys.base_prefix]
    system_root = environment.get('SystemRoot', environment.get('SYSTEMROOT'))
    if system_root:
        paths.extend((str(Path(system_root)/'System32'), system_root))
    environment['PATH'] = os.pathsep.join(dict.fromkeys(paths))
    return command, environment


def _activate(runtime, manifest_path):
    """Verify the prepared receipt before importing private extension modules."""
    if sys.platform != 'win32' or sys.version_info[:2] != (3, 14):
        raise RuntimeError('The private Gaussian runtime requires Windows Python 3.14')
    if not sys.flags.isolated or not sys.flags.no_site:
        raise RuntimeError('The Gaussian runtime requires an isolated fresh process')
    if 'torch' in sys.modules or any(name.startswith('onnxruntime') for name in sys.modules):
        raise RuntimeError('Gaussian and inference libraries must use separate processes')
    # Reuse the exact completed-file verifier, without importing the installer UI.
    import importlib.util
    root = Path(__file__).resolve().parents[1]
    specification = importlib.util.spec_from_file_location('surface_runtime_verifier',
                                                          root/'setup/steam_link_setup_room.py')
    helper = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(helper)
    raw = manifest_path.read_bytes()
    manifest = json.loads(raw)
    if (manifest.get('schema') != 'sfnp-room-surface-runtime-v1'
            or manifest.get('torch_version') != '2.9.1+cu128'
            or manifest.get('cuda_version') != '12.8'):
        raise ValueError('Unknown private Gaussian runtime')
    if not helper._completed_runtime(runtime, hashlib.sha256(raw).hexdigest(),
            schema='sfnp-room-surface-cache-v1', required=(
                'vendor/torch/__init__.py', 'vendor/torch/_C.cp314-win_amd64.pyd',
                'vendor/torch/lib/torch_cuda.dll', 'vendor/numpy/__init__.py',
                'vendor/torch-2.9.1+cu128.dist-info/LICENSE',
                'vendor/torch-2.9.1+cu128.dist-info/NOTICE')):
        raise ValueError('Private Gaussian preparation is incomplete')
    vendor = runtime/'vendor'
    directories = sorted({path.parent for path in vendor.rglob('*.dll')})
    handles = [os.add_dll_directory(str(path)) for path in directories]
    os.environ['PATH'] = os.pathsep.join([*(str(path) for path in directories), os.environ.get('PATH', '')])
    sys.path.insert(0, str(vendor))
    os.chdir(runtime)
    return manifest, handles


def _exercise(torch, device):
    """Use the actual renderer and optimizer, including backward and growth."""
    from .surface_gaussians import SurfaceGaussians
    from .surface_optimization import RayTrainingView, SurfaceOptimizer
    def tensor(value, dtype=torch.float32):
        return torch.tensor(value, dtype=dtype, device=device)
    pair = SurfaceGaussians(tensor([[0., 0., -1.], [0., 0., -2.]]),
                            [[0, 0, 1]]*2, [[.2, .2]]*2, [[1, 0, 0], [0, 0, 1]], .5)
    result = pair.render(tensor([[0., 0., 0.]]), tensor([[0., 0., -1.]]),
                         tensor([[1, 0]], torch.int64))
    if (abs(result['depth'].item()-4/3) > 2e-5 or abs(result['alpha'].item()-.75) > 2e-5
            or not torch.allclose(result['color'], tensor([[.5, 0., .25]]), atol=2e-5, rtol=0)):
        raise RuntimeError('Private Gaussian ray-render warmup failed')
    model = SurfaceGaussians(tensor([[0., 0., -2.12]]), [[0, 0, 1]], [[.2, .2]], [[.7]], .8)
    optimizer = SurfaceOptimizer(model, seed_directions=tensor([[0., 0., -1.]]),
                                 seed_position_sigma_m=.1, metric_weight=.1)
    view = RayTrainingView(tensor([[0., 0., 0.]]), tensor([[0., 0., -1.]]), tensor([[.7]]),
        tensor([[0]], torch.int64), tensor([True], torch.bool), tensor([False], torch.bool),
        measured_depth=tensor([2.]), measured_sigma=tensor([.02]))
    for _ in range(250):
        optimizer.step(view)
    error = abs(model.means[0, 2].item()+2.)
    if error >= .004:
        raise RuntimeError('Private Gaussian metric-gradient warmup failed')
    frozen = {name: value.detach().clone() for name, value in model.named_parameters()}
    grown = optimizer.extend(SurfaceGaussians(tensor([[1., 0., -2.]]), [[0, 0, 1]],
        [[.2, .2]], [[.6]], .8), seed_directions=tensor([[0., 0., -1.]]), seed_position_sigma_m=.1)
    if any(not torch.equal(value[:1], frozen[name]) for name, value in grown.named_parameters()):
        raise RuntimeError('Private Gaussian incremental-growth warmup failed')
    return error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--cpu-development', action='store_true')
    args = parser.parse_args()
    started = time.monotonic()
    runtime = args.runtime.resolve()
    manifest, handles = _activate(runtime, args.manifest.resolve())
    import numpy
    import torch
    if (not Path(torch.__file__).resolve().is_relative_to(runtime/'vendor/torch')
            or not Path(numpy.__file__).resolve().is_relative_to(runtime/'vendor/numpy')
            or torch.__version__ != manifest['torch_version'] or torch.version.cuda != manifest['cuda_version']):
        raise RuntimeError('Private Gaussian module identity changed')
    torch.set_num_threads(2)
    device = 'cpu' if args.cpu_development else 'cuda'
    if device == 'cuda':
        if not torch.cuda.is_available():
            raise RuntimeError('An NVIDIA GPU with a compatible driver is required for Gaussian scanning')
        total = torch.cuda.get_device_properties(0).total_memory
        if total < 3*1024**3:
            raise RuntimeError('Gaussian scanning requires an NVIDIA GPU with at least 3 GB of VRAM')
        torch.cuda.set_per_process_memory_fraction(min(.25, 1024**3/total), 0)
    error = _exercise(torch, device)
    if device == 'cuda':
        torch.cuda.synchronize()
    print(json.dumps({'schema': 'sfnp-room-surface-warmup-v1', 'result': 'PASS',
        'torch_version': torch.__version__, 'cuda_version': torch.version.cuda, 'device': device,
        'private_dll_directories': len(handles), 'isolated_process': True,
        'actual_gaussian_render_and_backward': True, 'metric_control_error_m': error,
        'incremental_state_preserved': True, 'gpu_ready': device == 'cuda',
        'nvidia_gpu_performance_verified': False, 'headset_test': False,
        'wall_seconds': time.monotonic()-started}, allow_nan=False), flush=True)


if __name__ == '__main__':
    main()
