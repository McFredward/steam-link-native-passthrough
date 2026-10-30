#!/usr/bin/env python3
"""Host-only CPU fixtures: no NVENC, V4L2, device probing or live services."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
import subprocess
import tempfile


def with_unused_reference_set(coded):
    """Add one unused explicit SPS set to an owned CPU IDR, leaving CABAC intact.

    NVIDIA emits this valid but unnecessary declaration in an all-IDR session.
    This fixture exercises it together with real multi-slice entropy decoding.
    """
    starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', coded))
    for index, start in enumerate(starts):
        body = start.end()
        if ((coded[body] >> 1) & 63) != 33:
            continue
        end = starts[index+1].start() if index+1 < len(starts) else len(coded)
        ebsp = coded[body+2:end]
        rbsp = bytearray()
        zeros = 0
        for byte in ebsp:
            if zeros == 2 and byte == 3:
                zeros = 0
                continue
            rbsp.append(byte)
            zeros = min(zeros+1, 2) if byte == 0 else 0
        bits = ''.join(f'{byte:08b}' for byte in rbsp)
        position = 0

        def u(count):
            nonlocal position
            value = int(bits[position:position+count], 2)
            position += count
            return value

        def ue():
            zeros = 0
            while not u(1):
                zeros += 1
            return (1 << zeros)-1 + (u(zeros) if zeros else 0)

        u(4)
        assert u(3) == 0 and u(1) == 1  # One temporal layer.
        u(96)  # Complete Main profile_tier_level.
        ue()
        assert ue() == 1
        ue(); ue()
        assert u(1) == 0
        assert ue() == 0 and ue() == 0
        ue(); u(1)
        assert ue() >= 1  # The declared DPB has room for the unused entry.
        ue(); ue()
        for _ in range(6):
            ue()
        assert u(1) == 0  # No custom scaling list.
        u(1); u(1)
        assert u(1) == 0  # No PCM.
        offset = position
        assert ue() == 0
        # num_sets=1, negative=1, positive=0, delta_minus1=0, used=false.
        bits = bits[:offset] + '010010110' + bits[position:]
        assert len(bits) % 8 == 0
        replacement = bytearray()
        zeros = 0
        for offset in range(0, len(bits), 8):
            byte = int(bits[offset:offset+8], 2)
            if zeros == 2 and byte <= 3:
                replacement.append(3)
                zeros = 0
            replacement.append(byte)
            zeros = min(zeros+1, 2) if byte == 0 else 0
        return coded[:body+2] + replacement + coded[end:]
    raise ValueError('Owned CPU fixture has no SPS')


def generate(ffmpeg, root):
    root.mkdir(parents=True, exist_ok=True)
    # Fixed owned streams exercise independent non-WPP slices; libx265's
    # multi-slice mode requires WPP and cannot cover the production path.
    for variant, count in [('2', 2), ('4', 4), ('sao-2', 2), ('cross-2', 2), ('cross-sao-2', 2)]:
        source = Path(__file__).parent / f'independent-slices-{variant}'
        manifest = json.loads((source / 'manifest.json').read_bytes())
        assert manifest['extent'] == [256, 256] and manifest['slices'] == count
        assert manifest['frames'] == 16 and not manifest['wpp'] and not manifest['tiles']
        assert manifest.get('base_normalization_encoder_reconstruction_equal', manifest['encoder_reconstruction_all_YUV_bytes_equal'])
        assert manifest.get('reference_kind', 'encoder-reconstruction') in ('encoder-reconstruction', 'unmodified-serial-decoder')
        target = root / f'independent-slices-{variant}'
        target.mkdir()
        for name, expected in manifest['files'].items():
            raw = (source / name).read_bytes()
            assert len(raw) == expected['bytes'] and hashlib.sha256(raw).hexdigest() == expected['sha256']
            shutil.copyfile(source / name, target / name)
        (target / 'reference.sha256').write_text('\n'.join(manifest['reference_Y_sha256']) + '\n')
    params = 'qp=0:keyint=30:min-keyint=30:bframes=0:repeat-headers=1:annexb=1:range=full:info=0:aud=0:wpp=0:frame-threads=1:pools=none:slices=1:log-level=error'
    with tempfile.TemporaryDirectory(prefix='sfnp-hevc-fixture-') as temporary:
        for name, width, y, extra in (
                ('zero', 64, bytes(4096), []),
                ('opaque', 64, bytes([255])*4096, []),
                ('codes', 64, bytes(range(256))*16, []),
                ('crop', 62, bytes(range(248))*16, []),
                ('limited', 64, bytes(range(256))*16, ['-color_range', 'tv']),
                ('slices', 256, bytes(range(256))*256, []),
                ('tall-slices', 1152, bytes(range(256))*(1152*4608//256), [])):
            height = len(y)//width
            source = Path(temporary) / (name + '.yuv')
            source.write_bytes(y + bytes([128])*(width*height//2))
            local_params = params.replace('range=full', 'range=limited') if name == 'limited' else params
            if name in ('slices', 'tall-slices'):
                local_params = local_params.replace('slices=1', 'slices=2').replace('wpp=0', 'wpp=1').replace('pools=none', 'pools=1')
            command = [str(ffmpeg), '-v', 'error', '-nostdin', '-y', '-f', 'rawvideo', '-pixel_format', 'yuv420p',
                       '-video_size', f'{width}x{height}', '-framerate', '90', '-color_range', 'pc', '-i', str(source), '-frames:v', '1',
                       '-c:v', 'libx265', '-preset', 'ultrafast', '-tune', 'zerolatency', '-x265-params', local_params,
                       '-color_range', 'pc', *extra, '-f', 'hevc', str(root / (name + '.hevc'))]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f'{name}: exit {result.returncode}: {result.stderr.strip()}')
            if name in ('slices', 'tall-slices'):
                target = root / (name + '.hevc')
                target.write_bytes(with_unused_reference_set(target.read_bytes()))
        source = Path(temporary) / 'temporal.yuv'
        frames = []
        for frame in range(16):
            y = bytes(((x + frame*11) % 256 if (x//64+frame) % 5 else 0) for x in range(4096))
            (root / f'temporal-{frame:03d}.r8').write_bytes(y)
            frames.append(y + bytes([128])*2048)
        source.write_bytes(b''.join(frames))
        target = Path(temporary) / 'temporal.hevc'
        command = [str(ffmpeg), '-v', 'error', '-nostdin', '-y', '-f', 'rawvideo', '-pixel_format', 'yuv420p',
                   '-video_size', '64x64', '-framerate', '90', '-color_range', 'pc', '-i', str(source), '-frames:v', '16',
                   '-c:v', 'libx265', '-preset', 'ultrafast', '-tune', 'zerolatency', '-x265-params',
                   params.replace('keyint=30:min-keyint=30', 'keyint=8:min-keyint=8') + ':scenecut=0:open-gop=0:ref=1:weightp=0:temporal-mvp=0',
                   '-color_range', 'pc', '-f', 'hevc', str(target)]
        subprocess.run(command, check=True, capture_output=True, timeout=30)
        # Repeat the exact parameter sets for each picture, as requested from
        # NVENC by production. Keep individual AUs, never a decoder parser guess.
        coded = target.read_bytes()
        starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', coded))
        parameter_sets = []
        pictures = []
        for index, start in enumerate(starts):
            end = starts[index+1].start() if index+1 < len(starts) else len(coded)
            body = start.end(); kind = (coded[body] >> 1) & 63
            if kind == 32:
                parameter_sets = []
            if kind in (32, 33, 34):
                parameter_sets.append(coded[start.start():end])
            elif kind in (1, 19, 20):
                if coded[body+2] & 128:
                    pictures.append(bytearray(b''.join(parameter_sets)))
                pictures[-1].extend(coded[start.start():end])
            else:
                raise ValueError(f'Unexpected temporal fixture NAL {kind}')
        if len(pictures) != 16:
            raise ValueError('Temporal CPU fixture must contain exactly 16 pictures')
        for frame, picture in enumerate(pictures):
            (root / f'temporal-{frame:03d}.hevc').write_bytes(picture)
    return root


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.ffmpeg, args.output)
