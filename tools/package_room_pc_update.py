#!/usr/bin/env python3
"""Compose the frozen roomscan.17 payload with the released PC 0.3.13 fix.

Never install or contact a device. Preserve every Frame/setup/runtime byte and
record the separate source origins instead of claiming a reconstruction rebuild.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import importlib.util
import io
import json
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ROOM = '6102646b3364993bbda3510f74fd86c60a3a9965'
STABLE = '2f97bc558aba065e65ad793f0afff652525794ba'
ARCHIVE = 'distribution/steam-link-native-passthrough.zip'
DLL = 'pc-provider/bin/win64/driver_steam_link_native_passthrough.dll'
VERSION = '0.3.19-roomscan.17.1'
PC_SHA = 'ab11a3cf43f86c9d32f2a96a970707c5322a86b62d55112d164ff3e83f32a57c'


def pin(raw):
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def encoded(value):
    return (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def read_package(raw):
    assert len(raw) <= 128 * 1024**2
    archive = zipfile.ZipFile(io.BytesIO(raw))
    entries = archive.infolist()
    assert len(entries) < 4096 and sum(e.file_size for e in entries) <= 192 * 1024**2
    assert len({e.filename for e in entries}) == len(entries)
    assert all(e.file_size <= 32 * 1024**2 for e in entries)
    files = {e.filename: archive.read(e) for e in entries}
    outer = json.loads(files['bootstrap-package.json'])
    assert outer['files'] == {n: pin(b) for n, b in files.items() if n != 'bootstrap-package.json'}
    bundle = json.loads(files['stream-bridge-bundle.json'])
    assert all(pin(files[n]) == p for n, p in bundle['files'].items())
    return archive, files


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build(output):
    assert not git('status', '--porcelain', '--untracked-files=no').strip(), 'Commit sources first.'
    revision = git('rev-parse', 'HEAD').decode().strip()
    room_raw = git('show', ROOM + ':' + ARCHIVE)
    stable_raw = git('show', STABLE + ':' + ARCHIVE)
    assert pin(room_raw)['sha256'] == '66d9862ad3a4a74b848905b5ea5bdb6ee2891028c4706e8efc1fe03dff9dc5e6'
    assert pin(stable_raw)['sha256'] == '300dd70c41b0e9bc64404550675f165b8fa032294273ad825b0e85db0c0a4212'
    room_zip, files = read_package(room_raw)
    _, stable = read_package(stable_raw)
    before = dict(files)
    source = json.loads(files['stream-bridge-package-source.json'])
    base_revision = source['source_revision']
    for name, identity in source['source_pins'].items():
        assert pin(git('show', base_revision + ':' + name)) == identity, name
    stable_source = json.loads(stable['stream-bridge-package-source.json'])
    assert pin(stable[DLL])['sha256'] == PC_SHA
    assert stable_source['native_artifacts']['pc_provider'] == pin(stable[DLL])
    assert stable_source['pc_patch_validation']['confirmed_headset_fix'] is True
    assert not git('diff', '--name-only', stable_source['source_revision'], revision, '--', 'pc', 'bridge').strip()
    common_frame_changes = {name for name in files if name in stable
                            and name.startswith('frame/') and files[name] != stable[name]}
    assert common_frame_changes == {'frame/logging-default.conf'}
    versions = json.loads((ROOT / 'PATCH_VERSIONS.json').read_bytes())
    assert (ROOT / 'VERSION').read_text().strip() == VERSION
    assert versions == {'schema': 'sfnp-patch-versions-v1', 'pc': '0.3.13', 'frame': '0.3.13-roomscan.16'}
    files[DLL] = stable[DLL]
    for name in ('VERSION', 'PATCH_VERSIONS.json'):
        files[name] = (ROOT / name).read_bytes()
    bundle = json.loads(files['stream-bridge-bundle.json'])
    assert bundle['version'] == '0.3.16-roomscan.17'
    assert bundle['frame']['version'] == versions['frame']
    assert bundle['logging'] == {'channel': 'dev', 'level': 'Debug'}
    bundle['version'] = VERSION
    bundle['pc']['version'] = versions['pc']
    bundle['files'][DLL] = pin(files[DLL])
    files['stream-bridge-bundle.json'] = encoded(bundle)
    files['README.txt'] = before['README.txt'].replace(
        b'installer 0.3.16-roomscan.17', ('installer ' + VERSION).encode()).replace(
        b'PC patch 0.3.12-roomscan.8', b'PC patch 0.3.13') + (
        b'\nMAINTENANCE UPDATE: confirmed PC 0.3.13 texture-edge rounding fix.\n'
        b'Room scanning remains the original roomscan.17 test implementation.\n'
        b'All Frame files, camera helpers, setup code and private dependencies are unchanged.\n'
        b'The reconstruction rebuild and candidate 18 are not included.\n'
    )
    report = {
        'schema': 'sfnp-room-pc-maintenance-v1', 'version': VERSION,
        'source_revision': revision, 'room_package': {'revision': ROOM, **pin(room_raw)},
        'pc_package': {'revision': STABLE, **pin(stable_raw)},
        'pc_provider': pin(files[DLL]), 'component_versions': {s: versions[s] for s in ('pc', 'frame')},
        'frame_entries_unchanged': sum(n.startswith('frame/') for n in files),
        'setup_entries_unchanged': sum(n.startswith('setup/') for n in files),
        'room_reconstruction_rebuilt': False, 'native_binaries_rebuilt': False,
        'new_combination_headset_tested': False, 'device_actions': False,
        'pc_confirmation_scope': stable_source['hardware_verification_scope'],
    }
    source.update(base_package=report['room_package'], base_source_revision=base_revision,
                  base_runtime_profile=source.pop('runtime_profile'),
                  base_validation_receipts=source.pop('validation_receipts'), validation_receipts=[],
                  source_revision=revision, version=VERSION, component_versions=report['component_versions'],
                  hardware_verified=False, maintenance=report)
    source['source_origins'] = {name: base_revision for name in source['source_pins']}
    for name in ('VERSION', 'PATCH_VERSIONS.json'):
        source['source_pins'][name] = pin(files[name])
        source['source_origins'][name] = revision
    source['native_artifacts']['pc_provider'] = pin(files[DLL])
    source['pc_patch_validation'] = stable_source['pc_patch_validation']
    source['pc_native_source_revision'] = stable_source['source_revision']
    for name, identity in source['source_pins'].items():
        assert pin(git('show', source['source_origins'][name] + ':' + name)) == identity, name
    files['stream-bridge-package-source.json'] = encoded(source)
    outer = json.loads(files['bootstrap-package.json'])
    outer['files'] = {n: pin(b) for n, b in files.items() if n != 'bootstrap-package.json'}
    files['bootstrap-package.json'] = encoded(outer)
    changed = {n for n in files if files[n] != before[n]}
    assert changed == {DLL, 'VERSION', 'PATCH_VERSIONS.json', 'README.txt',
                       'stream-bridge-bundle.json', 'stream-bridge-package-source.json', 'bootstrap-package.json'}
    for name in files:
        if name.startswith(('frame/', 'setup/')):
            assert files[name] == before[name], name
    engine = load('room_maintenance_engine', ROOT / 'setup/steam_link_setup.py')
    contract = load('room_maintenance_contract', ROOT / 'setup/stream_bridge_install.py')
    with tempfile.TemporaryDirectory(prefix='sfnp-room-maintenance-') as temporary:
        tree = Path(temporary)
        for name, raw in files.items():
            target = tree / engine.relative(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
        contract.Bundle(tree, engine).admit('Windows')
        (tree / DLL).write_bytes(before[DLL])
        try:
            contract.Bundle(tree, engine)
        except engine.SetupError:
            pass
        else:
            raise AssertionError('Old PC binary must be rejected under the new pins.')
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w') as out:
        for entry in room_zip.infolist():
            out.writestr(copy.copy(entry), files[entry.filename])
    raw = result.getvalue()
    read_package(raw)
    report.update(archive=pin(raw), changed_entries=sorted(changed),
                  installed_bundle_admission='PASS', old_pc_tamper_control='refused')
    receipt = json.loads(git('show', ROOM + ':' + ARCHIVE + '.receipt.json'))
    receipt.update(version=VERSION, archive=pin(raw), source_revision=revision,
                   component_versions=report['component_versions'], source_pins=source['source_pins'],
                   source_origins=source['source_origins'], artifacts=source['native_artifacts'],
                   base_runtime_profile=receipt.pop('runtime_profile'),
                   base_validation_receipts=receipt.pop('validation_receipts'), validation_receipts=[],
                   hardware_verified=False, maintenance=report)
    output.parent.mkdir(parents=True, exist_ok=True)
    for path, data in ((output, raw), (output.with_suffix('.zip.receipt.json'), encoded(receipt)),
                       (output.with_suffix('.maintenance.json'), encoded(report))):
        with path.open('xb') as stream:
            stream.write(data)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args().output)
