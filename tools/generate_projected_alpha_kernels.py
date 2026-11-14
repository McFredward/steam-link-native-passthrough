#!/usr/bin/env python3
"""Embed only the owned CS5 kernels exported by our actual Windows fixture."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess


def checked_compute(data):
    if not 48 <= len(data) <= 65536 or data[:4] != b'DXBC':
        raise ValueError('Expected a bounded owned DXBC container')
    total, count = struct.unpack_from('<II', data, 24)
    if total != len(data) or not 1 <= count <= 32 or 32 + 4 * count > len(data):
        raise ValueError('Invalid owned shader container')
    programs = 0
    for index in range(count):
        offset = struct.unpack_from('<I', data, 32 + 4 * index)[0]
        if offset + 12 > len(data):
            raise ValueError('Invalid owned shader chunk')
        size = struct.unpack_from('<I', data, offset + 4)[0]
        if size > len(data) - offset - 8:
            raise ValueError('Invalid owned shader chunk extent')
        if data[offset:offset + 4] in (b'SHDR', b'SHEX'):
            version = struct.unpack_from('<I', data, offset + 8)[0]
            if version != 0x50050:
                raise ValueError('Only our compute-shader5.0 programs may be embedded')
            programs += 1
    if programs != 1:
        raise ValueError('Missing/ambiguous owned compute program')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--ci-run', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.source_commit) or args.ci_run <= 0:
        parser.error('An exact source commit and successful owned-fixture CI run are required')
    root = Path(__file__).resolve().parents[1]
    relative = 'pc/stream-bridge-projected-alpha-replay.h'
    source = subprocess.check_output(['git', 'show', args.source_commit + ':' + relative], cwd=root).decode()
    current = (root / relative).read_text()
    lines = ['#pragma once', '// Generated ONLY from our HLSL; contains no native runtime shader.',
             f'// Source {args.source_commit}; Windows fixture run {args.ci_run}.',
             '#include <array>', '#include <cstdint>',
             'namespace stream_bridge::sender::projected_alpha::kernels {']
    for bits, symbol in [(64, 'Clone64Hlsl'), (80, 'Clone80Hlsl'), (32, 'CloneResolve32Hlsl')]:
        expression = r'constexpr char ' + symbol + r'\[\]=R"\((.*?)\)";'
        recorded = re.findall(expression, source, re.S)
        actual = re.findall(expression, current, re.S)
        if len(recorded) != 1 or recorded != actual:
            raise ValueError('Owned HLSL differs from its exported source revision: ' + symbol)
        data = (args.input_dir / f'projected-alpha-clone{bits}.dxbc').read_bytes()
        checked_compute(data)
        lines.append(f'// HLSL SHA256 {hashlib.sha256(actual[0].encode()).hexdigest()}')
        lines.append(f'// DXBC SHA256 {hashlib.sha256(data).hexdigest()}')
        lines.append(f'inline constexpr std::array<std::uint8_t,{len(data)}> Clone{bits}{{{{')
        for at in range(0, len(data), 12):
            lines.append('    ' + ','.join(f'0x{value:02x}' for value in data[at:at + 12]) + ',')
        lines.append('}};')
    lines.append('} // namespace stream_bridge::sender::projected_alpha::kernels')
    args.output.write_text('\n'.join(lines) + '\n')
    print('Embedded three owned compute kernels; actual D3D11 execution remains required')


if __name__ == '__main__':
    main()
