"""Own a pinned native trainer process. No cloud service or Frame modification.

This first adapter measures fixed-dataset training and later meshing. It does
not claim that the upstream CLI can append camera views to a running trainer.
"""
from __future__ import annotations

from dataclasses import dataclass, asdict
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import shutil
import stat
import subprocess
import threading
import time


class ReconstructionError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise ReconstructionError(message)


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024*1024), b''):
            result.update(block)
    return result.hexdigest()


def read_json(path, limit=8*1024*1024):
    require(Path(path).stat().st_size <= limit, 'Reconstruction metadata exceeds its limit')
    with Path(path).open('rb') as source:
        raw = source.read(limit+1)
    require(len(raw) <= limit, 'Reconstruction metadata exceeds its limit')
    def invalid(_):
        raise ReconstructionError('Nonfinite reconstruction metadata')
    return json.loads(raw, parse_constant=invalid)


def write_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix+'.tmp')
    with temporary.open('x', encoding='utf-8') as output:
        json.dump(value, output, indent=2, allow_nan=False)
        output.write('\n')
    temporary.chmod(0o600)
    os.replace(temporary, path)


@dataclass(frozen=True)
class NvidiaGpu:
    index: int
    name: str
    total_mib: int
    free_mib: int
    uuid: str


def parse_nvidia_gpus(raw):
    require(type(raw) is str and len(raw) <= 32768, 'Invalid NVIDIA inventory')
    result = []
    for row in csv.reader(io.StringIO(raw)):
        require(len(result) < 16 and len(row) == 5, 'Invalid NVIDIA inventory row')
        index, name, total, free, uuid = (value.strip() for value in row)
        require(index.isdecimal() and total.isdecimal() and free.isdecimal()
                and 0 <= int(index) < 64 and 0 < int(free) <= int(total) <= 1024*1024
                and 0 < len(name) <= 160 and uuid.startswith('GPU-') and len(uuid) <= 80,
                'NVIDIA memory information is unavailable')
        result.append(NvidiaGpu(int(index), name, int(total), int(free), uuid))
    require(result and len({gpu.index for gpu in result}) == len(result), 'No usable NVIDIA GPU found')
    return result


def probe_nvidia():
    require(platform.system() == 'Windows', 'This prototype targets Windows and NVIDIA GPUs')
    executable = shutil.which('nvidia-smi.exe')
    if not executable:
        candidate = Path(os.environ.get('SystemRoot', r'C:\Windows'))/'System32/nvidia-smi.exe'
        if candidate.is_file():
            executable = str(candidate)
    require(executable, 'NVIDIA driver tools are unavailable. Check the installed NVIDIA driver.')
    response = subprocess.run([executable, '--query-gpu=index,name,memory.total,memory.free,uuid',
        '--format=csv,noheader,nounits'], capture_output=True, text=True, timeout=15, check=False)
    require(response.returncode == 0, 'NVIDIA GPU inventory could not be read')
    return parse_nvidia_gpus(response.stdout)


@dataclass(frozen=True)
class Profile:
    name: str
    max_splats: int
    image_side: int
    cache: str = 'cpu'


def choose_profile(gpu):
    require(isinstance(gpu, NvidiaGpu) and 0 < gpu.free_mib <= gpu.total_mib, 'Invalid GPU budget')
    # Initial conservative budgets, not measured minimum-hardware guarantees.
    require(gpu.free_mib >= 3072, 'At least 3 GB of free GPU memory is needed for this prototype. Close GPU-heavy apps and retry.')
    if gpu.free_mib < 6144:
        return Profile('compact', 200000, 640)
    if gpu.free_mib < 10240:
        return Profile('balanced', 500000, 960)
    return Profile('detail', 1000000, 1280)


def snapshot_dataset(source, target):
    """Copy a bounded calibrated dataset; training never modifies its input."""
    source, target = Path(source).resolve(strict=True), Path(target)
    metadata = read_json(source/'transforms.json')
    require(type(metadata) is dict and metadata.get('camera_model') == 'PINHOLE',
            'The first PC adapter requires a calibrated pinhole dataset')
    allowed = {'camera_model','w','h','fl_x','fl_y','cx','cy','frames','ply_file_path'}
    require(set(metadata) <= allowed, 'Unsupported dataset fields; export a qualified capture snapshot')
    width, height = metadata.get('w'), metadata.get('h')
    require(type(width) is int and type(height) is int and 8 <= width <= 4096 and 8 <= height <= 4096
            and width*height <= 8*1024*1024, 'Invalid training image extent')
    for name in ('fl_x','fl_y','cx','cy'):
        value = metadata.get(name)
        require(type(value) in (int,float) and math.isfinite(value), 'Invalid camera calibration')
    require(0 < metadata['fl_x'] <= 100000 and 0 < metadata['fl_y'] <= 100000
            and 0 <= metadata['cx'] < width and 0 <= metadata['cy'] < height, 'Camera calibration does not match the images')
    frames = metadata.get('frames')
    require(type(frames) is list and 2 <= len(frames) <= 1200, 'A dataset needs between 2 and 1200 calibrated images')
    paths, total = set(), 0
    target.mkdir(mode=0o700)
    try:
        def copy_file(name, maximum):
            nonlocal total
            require(type(name) is str and 0 < len(name) < 200 and '\\' not in name and ':' not in name, 'Invalid dataset image path')
            relative = Path(name)
            require(not relative.is_absolute() and '..' not in relative.parts and name not in paths, 'Duplicate or escaping dataset path')
            path = source/relative
            resolved = path.resolve(strict=True)
            require(resolved.is_relative_to(source) and not path.is_symlink() and stat.S_ISREG(path.stat().st_mode), 'Dataset files must be local regular files')
            size = path.stat().st_size
            require(0 < size <= maximum, 'Dataset file exceeds its budget')
            total += size
            require(total <= 2*1024*1024*1024, 'Dataset exceeds its disk budget')
            destination = target/relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
            destination.chmod(0o600)
            paths.add(name)
        for frame in frames:
            require(type(frame) is dict and set(frame) == {'file_path','transform_matrix'}, 'Invalid calibrated image record')
            matrix = frame['transform_matrix']
            require(type(matrix) is list and len(matrix) == 4 and all(type(row) is list and len(row) == 4 for row in matrix), 'Invalid camera pose extent')
            require(all(type(value) in (int,float) and math.isfinite(value) and abs(value) <= 1000 for row in matrix for value in row)
                    and matrix[3] == [0,0,0,1], 'Invalid camera pose')
            for a in range(3):
                for b in range(3):
                    require(abs(sum(matrix[r][a]*matrix[r][b] for r in range(3))-(a == b)) < 1e-4, 'Camera pose is not rigid')
            determinant = sum(matrix[0][a]*(matrix[1][(a+1)%3]*matrix[2][(a+2)%3]-matrix[1][(a+2)%3]*matrix[2][(a+1)%3]) for a in range(3))
            require(determinant > .9999, 'Mirrored camera pose')
            require(frame['file_path'].lower().endswith('.png'), 'The first PC adapter requires PNG images')
            copy_file(frame['file_path'], 32*1024*1024)
            with (target/frame['file_path']).open('rb') as image:
                header = image.read(24)
            import struct
            require(len(header) == 24 and header[:8] == b'\x89PNG\r\n\x1a\n'
                    and struct.unpack('>II',header[16:24]) == (width,height), 'Training image dimensions differ from calibration')
        if 'ply_file_path' in metadata:
            copy_file(metadata['ply_file_path'], 64*1024*1024)
        write_json(target/'transforms.json', metadata)
        return {'images':len(frames), 'width':width, 'height':height, 'bytes':total,
                'transforms_sha256':digest(target/'transforms.json')}
    except BaseException:
        shutil.rmtree(target)
        raise


def verify_backend(path, manifest, development=False):
    path = Path(path).absolute()
    require(path.is_file() and not path.is_symlink(), 'Native Gaussian backend is missing')
    key = 'linux_development' if development else 'windows'
    require(development or platform.system() == 'Windows', 'The installed backend targets Windows')
    pin = manifest['platforms'][key]['binary']
    require(path.stat().st_size == pin['bytes'] and digest(path) == pin['sha256'], 'Native Gaussian backend does not match its pinned build')
    return path


def train_arguments(executable, dataset, output, profile, iterations, image_side, *, development=False):
    require(type(iterations) is int and 2 <= iterations <= 60000, 'Invalid reconstruction step budget')
    divisor = max(1, math.ceil(image_side/profile.image_side))
    return [str(executable), 'train', 'meshing', '--lang', 'en', '--data', str(dataset),
        '--data-format', 'nerfstudio', '--output-dir-prefix', str(output.parent), '--output-dir-name', output.name,
        '--num-iterations', str(iterations), '--cap-max', str(profile.max_splats), '--cache-images', profile.cache,
        '--train-resolution-divisor', str(divisor), '--max-train-batch-size', '1', '--train-frame', 'points',
        '--scene-center', 'none', '--auto-scale-poses', '0', '--relative-scale', '1',
        '--disable-viewer', '1', '--keep-viewer-alive', '0', '--eval-lpips', '0', '--log-performance', '1',
        '--save-full-checkpoint', '1', '--steps-per-save', str(min(200, iterations)),
        '--device', 'auto' if development else 'NVIDIA']


def run_owned(arguments, log, cancel=None):
    """Only the owned reconstruction child is stopped; the last checkpoint stays."""
    cancel = cancel or threading.Event()
    with Path(log).open('xb') as output:
        child = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
            close_fds=True, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0)
        try:
            while child.poll() is None:
                if cancel.wait(.1):
                    raise ReconstructionError('Reconstruction stopped; completed checkpoints are preserved')
                require(output.tell() <= 32*1024*1024, 'Reconstruction log exceeds its budget')
            require(child.returncode == 0, 'Native reconstruction failed. See the saved reconstruction log.')
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=3)


def require_metric_frame(run):
    transform = read_json(Path(run)/'scene_transform.json')
    require(transform.get('format') == 'spirula-scene-transform' and transform.get('version') == 1, 'Missing reconstruction coordinate proof')
    for name in ('train_from_world','world_from_train'):
        value = transform[name]
        matrix = value['matrix_4x4']
        require(abs(value['scale']-1) < 1e-6 and len(matrix) == 4
                and all(len(row) == 4 for row in matrix)
                and all(type(x) in (int,float) and math.isfinite(x) and abs(x-(r == c)) < 1e-6
                        for r,row in enumerate(matrix) for c,x in enumerate(row)),
                'Reconstruction changed the metric room coordinate frame')


def latest_checkpoint(run):
    candidates = [p for p in Path(run).glob('step-*.ckpt') if p.is_dir() and not p.is_symlink()]
    require(0 < len(candidates) <= 301, 'No completed Gaussian checkpoint found')
    checkpoint = max(candidates, key=lambda p: p.name)
    splats = checkpoint/'splat.ply'
    require(splats.is_file() and not splats.is_symlink() and 0 < splats.stat().st_size <= 1024*1024*1024,
            'The Gaussian checkpoint is unavailable or exceeds its budget')
    return checkpoint


def ply_counts(path):
    with Path(path).open('rb') as source:
        require(source.readline(64) == b'ply\n', 'Invalid reconstruction PLY signature')
        counts = {}
        total = 0
        for _ in range(128):
            line = source.readline(1025)
            total += len(line)
            require(0 < len(line) <= 1024 and total < 32768, 'Invalid reconstruction PLY header')
            if line == b'end_header\n':
                require('vertex' in counts, 'Reconstruction PLY has no vertices')
                return counts
            if line.startswith(b'element '):
                parts = line.split()
                require(len(parts) == 3 and parts[1] in (b'vertex',b'face') and parts[2].isdigit(), 'Invalid PLY element')
                count = int(parts[2])
                require(count <= 4000000, 'Reconstruction PLY element budget reached')
                counts[parts[1].decode('ascii')] = count
    raise ReconstructionError('Incomplete reconstruction PLY header')


def train(executable, source, directory, profile, iterations=2000, *, manifest, cancel=None, development=False):
    executable = verify_backend(executable, manifest, development)
    directory = Path(directory).absolute()
    directory.mkdir(mode=0o700)
    receipt = {'schema':'sfnp-pc-gaussian-test-v1','state':'preparing','scope':'fixed-dataset PC prototype',
               'live_frame_integration':False,'headset_accuracy_verified':False,'profile':asdict(profile),
               'backend_sha256':digest(executable)}
    write_json(directory/'result.json', receipt)
    try:
        snapshot = directory/'dataset'
        info = snapshot_dataset(source, snapshot)
        receipt.update(state='training', dataset=info)
        write_json(directory/'result.json', receipt)
        run = directory/'model'
        args = train_arguments(executable,snapshot,run,profile,iterations,max(info['width'],info['height']),development=development)
        begin = time.monotonic()
        run_owned(args,directory/'training.log',cancel)
        receipt.update(training_wall_seconds=time.monotonic()-begin)
        require_metric_frame(run)
        checkpoint = latest_checkpoint(run)
        counts = ply_counts(checkpoint/'splat.ply')
        require(counts['vertex'] > 0, 'The trainer produced no Gaussians')
        receipt.update(state='trained', checkpoint=str(checkpoint.relative_to(directory)),
                       splat_bytes=(checkpoint/'splat.ply').stat().st_size, splats=counts['vertex'],
                       metric_frame_preserved=True, surface_mesh_ready=False)
        write_json(directory/'result.json',receipt)
        return receipt
    except BaseException:
        receipt['state'] = 'stopped' if cancel and cancel.is_set() else 'failed'
        write_json(directory/'result.json',receipt)
        raise


def mesh(executable, directory, *, manifest, cancel=None, development=False):
    executable = verify_backend(executable,manifest,development)
    directory = Path(directory).absolute()
    require_metric_frame(directory/'model')
    checkpoint = latest_checkpoint(directory/'model')
    target = directory/'mesh'
    target.mkdir(mode=0o700)
    args = [str(executable),'mesh',str(checkpoint),'--lang','en','--data',str(directory/'dataset'),
        '--data-format','nerfstudio','--output',str(target/'room'),'--format','ply','--color','none',
        '--cull-unseen','1','--fill-hole-ratio','0','--fill-hole-max-edges','0',
        '--max-grid-res','256','--num-threads','4','--device','auto' if development else 'NVIDIA']
    run_owned(args,directory/'meshing.log',cancel)
    require((target/'room.ply').is_file(), 'The backend did not produce a surface mesh')
    counts = ply_counts(target/'room.ply')
    require(counts.get('vertex',0) > 0 and counts.get('face',0) > 0,
            'The current Gaussian model has no extractable surface mesh yet; more observations or refinement are needed')
    receipt = read_json(directory/'result.json')
    receipt.update(surface_mesh_ready=True,mesh_vertices=counts['vertex'],mesh_faces=counts['face'])
    write_json(directory/'result.json',receipt)
    return target/'room.ply'
