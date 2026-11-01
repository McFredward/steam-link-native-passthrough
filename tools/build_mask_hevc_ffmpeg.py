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


def source_patches(source, manifest, enabled=True):
    """Apply reviewed byte-pinned backports without accepting an editable cache."""
    specs = json.loads(manifest.read_text())
    if specs.get('schema') != 'sfnp-mask-ffmpeg-source-patches-v1' or not 1 <= len(specs['patches']) <= 4:
        raise RuntimeError('Unknown private decoder patch set')
    initial, final, patches = {}, {}, []
    for patch in specs['patches']:
        path = ROOT / patch['path']
        if not path.is_file() or pin(path) != patch['sha256']:
            raise RuntimeError('Private decoder patch pin differs')
        files = patch['files']
        if not files or any(not name.startswith('libavcodec/hevc/') or '..' in Path(name).parts
                            or Path(name).suffix not in ('.c', '.h') for name in files):
            raise RuntimeError('Private decoder patch source scope differs')
        for name, value in files.items():
            if name in final and final[name] != value['base_sha256']:
                raise RuntimeError('Private decoder patch chain differs')
            initial.setdefault(name, value['base_sha256'])
            final[name] = value['patched_sha256']
        patches.append((path, files))
    if any((source / name).is_symlink() or not (source / name).is_file() for name in final):
        raise RuntimeError('Private decoder patch source must be a regular file')
    state = {name: pin(source / name) for name in final}
    base = state == initial
    changed = state == final
    if not enabled:
        if not base:
            raise RuntimeError('Baseline decoder needs a separate unpatched output directory')
        return [], {}
    if not base and not changed:
        raise RuntimeError('Private decoder patched source differs')
    if base:
        # Validate the complete result before replacing any cached source file.
        with tempfile.TemporaryDirectory(prefix='sfnp-hevc-backport-') as directory:
            scratch = Path(directory)
            for name in final:
                target = scratch / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source / name, target)
            for path, files in patches:
                if any(pin(scratch / name) != value['base_sha256'] for name, value in files.items()):
                    raise RuntimeError('Private decoder patch predecessor differs')
                subprocess.run(['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch',
                                '-p1', '-i', str(path.resolve())], cwd=scratch, check=True, timeout=20)
                if any(pin(scratch / name) != value['patched_sha256'] for name, value in files.items()):
                    raise RuntimeError('Private decoder patch output differs')
            if any(pin(scratch / name) != digest for name, digest in final.items()):
                raise RuntimeError('Private decoder final source differs')
            for name in final:
                shutil.copyfile(scratch / name, source / name)
    return specs['patches'], final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--signature', type=Path, required=True)
    parser.add_argument('--key', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cross-prefix', default='aarch64-linux-gnu-')
    parser.add_argument('--upstream-only', action='store_true',
                        help='Host comparison only; use a separate unpatched output directory.')
    args = parser.parse_args()
    if args.upstream_only and args.cross_prefix:
        raise RuntimeError('Upstream-only is a host comparison, not a Frame build')
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
    patches, expected_patches = source_patches(source, ROOT / 'packaging/mask-ffmpeg-patches.json',
                                               not args.upstream_only)
    # A reused editable extraction is not provenance by itself. Check every
    # upstream regular source file against the authenticated release bytes.
    with tarfile.open(args.archive) as archive:
        for member in archive.getmembers():
            if member.isfile():
                actual=output/member.name
                relative = str(Path(member.name).relative_to('ffmpeg-' + lock['version']))
                expected = expected_patches.get(relative) or hashlib.sha256(archive.extractfile(member).read()).hexdigest()
                if not actual.is_file() or pin(actual) != expected:
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
        'source': lock, 'flags': flags, 'source_patches': patches,
        'libraries': {name: pin(prefix / name) for name in expected},
        'device_actions': False,
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
