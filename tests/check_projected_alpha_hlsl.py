#!/usr/bin/env python3
"""Compile our embedded HLSL locally; DXIL output is not D3D11 GPU evidence."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dxc', type=Path, help='Reviewed Microsoft DXC executable')
    args = parser.parse_args()
    compiler = args.dxc.resolve(strict=True)
    root = Path(__file__).resolve().parents[1]
    programs = [
        ('pc/stream-bridge-projected-alpha-replay.h', 'Clone64Hlsl', [('main', 'cs')]),
        ('pc/stream-bridge-projected-alpha-replay.h', 'Clone80Hlsl', [('main', 'cs')]),
        ('pc/stream-bridge-projected-alpha-replay.h', 'CloneResolve32Hlsl', [('main', 'cs')]),
        ('tests/test_stream_bridge_projected_alpha_gpu.cpp', 'Fixture', [
            ('vertex', 'vs'), ('quad', 'vs'), ('unitQuad', 'vs'), ('white', 'ps'),
            ('pixel', 'ps'), ('pixel64', 'ps'), ('copyRgba', 'ps'), ('compute', 'cs')]),
        ('tests/test_stream_bridge_projected_alpha_gpu.cpp', 'ResolveFixture', [('resolve', 'ps')]),
        ('tests/test_stream_bridge_projected_producer_windows.cpp', 'ProducerFixture', [('vs', 'vs'), ('layoutVS', 'vs'), ('ps', 'ps'), ('compute', 'cs'), ('outside', 'cs')]),
    ]
    checked = 0
    with tempfile.TemporaryDirectory(prefix='sfnp-owned-hlsl-') as temporary:
        directory = Path(temporary)
        for relative, name, entries in programs:
            expression = r'constexpr char ' + re.escape(name) + r'\[\]=R"\((.*?)\)";'
            matches = re.findall(expression, (root / relative).read_text(), re.S)
            if len(matches) != 1:
                raise SystemExit('Missing/ambiguous owned shader: ' + name)
            source = directory / (name + '.hlsl')
            source.write_text(matches[0])
            for entry, stage in entries:
                subprocess.run([
                    str(compiler), '-HV', '2016', '-WX', '-E', entry,
                    '-T', stage + '_6_0', '-Fo', str(directory / (name + '-' + entry + '.dxil')),
                    str(source),
                ], check=True)
                checked += 1
    print(f'Owned HLSL preflight PASS: {checked} entry points; actual Windows/D3D11 checks remain required')


if __name__ == '__main__':
    main()
