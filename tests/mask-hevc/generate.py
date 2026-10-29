#!/usr/bin/env python3
"""Host-only CPU fixtures: no NVENC, V4L2, device probing or live services."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def generate(ffmpeg, root):
    root.mkdir(parents=True, exist_ok=True)
    params = 'qp=0:keyint=30:min-keyint=30:bframes=0:repeat-headers=1:annexb=1:range=full:info=0:aud=0:wpp=0:frame-threads=1:pools=none:slices=1:log-level=error'
    with tempfile.TemporaryDirectory(prefix='sfnp-hevc-fixture-') as temporary:
        for name, width, y, extra in (
                ('zero', 64, bytes(4096), []),
                ('opaque', 64, bytes([255])*4096, []),
                ('codes', 64, bytes(range(256))*16, []),
                ('crop', 62, bytes(range(248))*16, []),
                ('limited', 64, bytes(range(256))*16, ['-color_range', 'tv'])):
            source = Path(temporary) / (name + '.yuv')
            source.write_bytes(y + bytes([128])*(width*64//2))
            local_params = params.replace('range=full', 'range=limited') if name == 'limited' else params
            command = [str(ffmpeg), '-v', 'error', '-nostdin', '-y', '-f', 'rawvideo', '-pixel_format', 'yuv420p',
                       '-video_size', f'{width}x64', '-framerate', '90', '-color_range', 'pc', '-i', str(source), '-frames:v', '1',
                       '-c:v', 'libx265', '-preset', 'ultrafast', '-tune', 'zerolatency', '-x265-params', local_params,
                       '-color_range', 'pc', *extra, '-f', 'hevc', str(root / (name + '.hevc'))]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stderr.strip())
    return root


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.ffmpeg, args.output)
