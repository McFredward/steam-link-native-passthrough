#!/usr/bin/env python3
"""One compile/link dependency list for local preflight and real Windows CI."""
import argparse
import os
from pathlib import Path
import subprocess

FIXTURES = [
    ('tests/test_stream_bridge_projected_alpha_gpu.cpp', 'projected-alpha',
     ['d3d11', 'dxgi', 'd3dcompiler', 'dxguid', 'uuid', 'bcrypt']),
    ('tests/test_stream_bridge_projected_channel_windows.cpp', 'projected-channel',
     ['bcrypt', 'dxguid', 'uuid']),
    ('tests/test_stream_bridge_projected_producer_windows.cpp', 'projected-producer',
     ['d3d11', 'dxgi', 'd3dcompiler', 'dxguid', 'uuid', 'bcrypt']),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', choices=['msvc', 'mingw'], required=True)
    parser.add_argument('--cxx', help='Explicit compiler executable')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = args.cxx or ('cl' if args.compiler == 'msvc' else 'x86_64-w64-mingw32-g++')
    for relative, name, libraries in FIXTURES:
        sources = [str(root / relative)]
        if name == 'projected-producer':
            sources.append(str(root / 'pc/stream-bridge-sender-provider-shaders.cpp'))
        if args.compiler == 'msvc':
            command = [compiler, '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/I' + str(root),
                       *sources, '/Fe:' + str(output / (name + '.exe')),
                       '/Fo:' + str(output) + os.sep, '/link', *[lib + '.lib' for lib in libraries]]
        else:
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Wshadow=local', '-Werror',
                       '-I' + str(root), *sources, '-o', str(output / (name + '.exe')),
                       *['-l' + lib for lib in libraries], '-static-libgcc', '-static-libstdc++']
        print(f'Compile/link {name} ({args.compiler}); no runtime execution', flush=True)
        subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
