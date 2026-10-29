#!/usr/bin/env python3
"""Offline, pinned LGPL HEVC decoder build. Never installs on a device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def pin(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--signature', type=Path, required=True)
    parser.add_argument('--key', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cross-prefix', default='aarch64-linux-gnu-')
    args = parser.parse_args()
    lock = json.loads((ROOT / 'packaging/mask-ffmpeg.json').read_text())
    for path, expected in ((args.archive, lock['sha256']),
                           (args.signature, lock['signature_sha256']),
                           (args.key, lock['key_sha256'])):
        if not path.is_file() or pin(path) != expected:
            raise RuntimeError('Pinned FFmpeg input differs: ' + str(path))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = output / 'install'
    with tempfile.TemporaryDirectory(prefix='sfnp-ffmpeg-key-') as directory:
        keyring = Path(directory) / 'release.gpg'
        subprocess.run(['gpg', '--batch', '--no-options', '--homedir', directory,
                        '--output', str(keyring), '--dearmor', str(args.key.resolve())],
                       check=True, timeout=20)
        verified = subprocess.run(['gpgv', '--homedir', directory, '--keyring', str(keyring),
                                   '--status-fd', '1', str(args.signature.resolve()),
                                   str(args.archive.resolve())], check=True, timeout=30,
                                  stdout=subprocess.PIPE, text=True)
        if not any(line.startswith('[GNUPG:] VALIDSIG ' + lock['fingerprint'] + ' ')
                   for line in verified.stdout.splitlines()):
            raise RuntimeError('FFmpeg release signature fingerprint differs')
    source = output / ('ffmpeg-' + lock['version'])
    if not source.exists():
        with tarfile.open(args.archive) as archive:
            archive.extractall(output, filter='data')
    # A reused editable extraction is not provenance by itself. Check every
    # upstream regular source file against the authenticated release bytes.
    with tarfile.open(args.archive) as archive:
        for member in archive.getmembers():
            if member.isfile():
                actual=output/member.name
                if not actual.is_file() or pin(actual)!=hashlib.sha256(archive.extractfile(member).read()).hexdigest():
                    raise RuntimeError('Extracted FFmpeg source differs: ' + member.name)
    build = output / 'objects'
    build.mkdir(exist_ok=True)
    flags = ['--prefix=' + str(prefix), '--build-suffix=-sfnp-mask',
             '--enable-shared', '--disable-static', '--disable-gpl', '--disable-nonfree',
             '--disable-version3', '--disable-autodetect', '--disable-everything',
             '--disable-programs', '--disable-doc', '--disable-debug', '--disable-network',
             '--disable-avdevice', '--disable-avfilter', '--disable-avformat',
             '--disable-swresample', '--disable-swscale', '--enable-avcodec',
             '--enable-avutil', '--enable-decoder=hevc', '--enable-pic',
             '--extra-ldflags=-Wl,-Bsymbolic-functions']
    if args.cross_prefix:
        flags += ['--enable-cross-compile', '--arch=aarch64', '--target-os=linux',
                  '--cross-prefix=' + args.cross_prefix]
    else:
        flags += ['--disable-x86asm']
    previous=output/'recipe.json'
    if previous.exists() and json.loads(previous.read_text())['flags']!=flags:
        shutil.rmtree(build)
        build.mkdir()
    subprocess.run([str(source / 'configure'), *flags], cwd=build, check=True, timeout=120)
    subprocess.run(['make', '-j4'], cwd=build, check=True, timeout=600)
    subprocess.run(['make', 'install'], cwd=build, check=True, timeout=60)
    expected = ('lib/libavcodec-sfnp-mask.so.62', 'lib/libavutil-sfnp-mask.so.60')
    for name in expected:
        if not (prefix / name).is_file():
            raise RuntimeError('Private decoder SONAME missing: ' + name)
    config = (build / 'config.h').read_text()
    if '#define FFMPEG_LICENSE "LGPL version 2.1 or later"' not in config:
        raise RuntimeError('Unexpected FFmpeg license configuration')
    (output / 'recipe.json').write_text(json.dumps({
        'source': lock, 'flags': flags, 'source_patches': [],
        'libraries': {name: pin(prefix / name) for name in expected},
        'device_actions': False,
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
