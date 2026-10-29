#!/usr/bin/env python3
"""Run real host-only HEVC mask fixtures and authenticated Process Ingress."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from generate import generate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('decoder', 'ingress', 'ffmpeg', 'codec', 'utility'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='sfnp-mask-hevc-') as directory:
        fixtures = generate(args.ffmpeg.resolve(), Path(directory))
        for executable in (args.decoder, args.ingress):
            result = subprocess.run([str(executable.resolve()), str(args.codec.resolve()), str(args.utility.resolve()), str(fixtures)], capture_output=True, text=True, timeout=30)
            print(result.stdout, end='')
            if result.returncode:
                raise RuntimeError(f'{executable.name} failed: {result.stderr}')


if __name__ == '__main__':
    main()
