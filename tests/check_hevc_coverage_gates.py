#!/usr/bin/env python3
"""Run production HEVC proof paths, then require causal gate removals to fail.

Uses existing successful Unix Makefiles test objects, compiling each changed
production translation unit independently. No devices, credentials or downloads.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def need(value, message):
    if not value:
        raise ValueError(message)

def pin(path):
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}

def run(command, cwd, log):
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=90)
    log.write_text(result.stdout)
    return result.returncode, result.stdout

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build_directory.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = [
        ('missing_probe', 'receiver/stream_bridge_native_runtime.cpp',
         '!peer.metadataObserved', 'false', 'test_stream_bridge_native_runtime',
         ['--hevc-gates'], 'confirmed capability alone cannot publish HEVC'),
        ('missing_negotiated_capability', 'receiver/stream_bridge_native_runtime.cpp',
         '!(receiver.negotiatedCapabilities()&stream_bridge::CapHevcIntraCoverage)',
         'false', 'test_stream_bridge_native_runtime', ['--hevc-gates'],
         'receiver capability check independently rejects MAC-valid Codec6'),
        ('implicit_opt_in', 'bridge/stream_bridge_control.cpp',
         'request.hevcIntraCoverageEnabled?request.offered:(request.offered&~CapHevcIntraCoverage)',
         'request.offered', 'test_stream_bridge_native_runtime', ['--hevc-gates'],
         'ready receiver cannot make a default lossless sender negotiate Codec6'),
        ('missing_encoded_payload_hash', 'bridge/stream_bridge_frame.cpp',
         'if(!equal({coverageHash.data(),coverageHash.size()},{h+112,32}))',
         'if((void(coverageHash),false))', 'test_stream_bridge_hevc_coverage', [],
         'mutated AU/certificate/payload never publishes new proof'),
    ]
    results = {}
    for target, arguments in sorted({(case[4], tuple(case[5])) for case in cases}):
        code, text = run([str(build / target), *arguments], build, output / (target + '-baseline.log'))
        need(code == 0, 'Original production target failed: ' + target)
        results[target] = {'exit_status': code, 'baseline': True}
    for name, relative, before, after, target, arguments, witness in cases:
        directory = output / name
        directory.mkdir(exist_ok=True)
        source = (ROOT / relative).read_text()
        need(source.count(before) == 1, 'Production gate changed; audit mutation: ' + name)
        changed = directory / Path(relative).name
        changed.write_text(source.replace(before, after))
        # Local header names and ../bridge or ../receiver include spellings
        # resolve against the actual owned sources without copying SDKs.
        for parent in ('bridge', 'receiver', 'pc'):
            alias = output / parent
            if not alias.exists():
                alias.symlink_to(ROOT / parent, target_is_directory=True)
        link_file = build / 'CMakeFiles' / (target + '.dir') / 'link.txt'
        command = shlex.split(link_file.read_text())
        obj = directory / 'mutated.o'
        code, _ = run([command[0], '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                       '-I', str(ROOT), '-I', str((ROOT / relative).parent), '-c', str(changed), '-o', str(obj)],
                      build, directory / 'compile.log')
        need(code == 0, 'Causal control did not compile: ' + name)
        if relative.startswith('receiver/'):
            matches = [i for i, arg in enumerate(command) if arg.endswith('/receiver/stream_bridge_native_runtime.cpp.o')]
            need(len(matches) == 1, 'Native test link shape changed')
            command[matches[0]] = str(obj)
        else:
            # The explicit object satisfies the complete translation unit;
            # only unrelated archive members are then pulled in by the linker.
            command.insert(1, str(obj))
        executable = directory / target
        command[command.index('-o') + 1] = str(executable)
        code, _ = run(command, build, directory / 'link.log')
        need(code == 0, 'Causal control did not link: ' + name)
        code, text = run([str(executable), *arguments], build, directory / 'result.log')
        need(code != 0 and witness in text, 'Gate removal lacked its exact expected negative witness: ' + name)
        results[name] = {'exit_status': code, 'expected_negative': True, 'witness': witness,
                         'production_source': relative, 'production_pin': pin(ROOT / relative)}
    report = {'schema': 'sfnp-hevc-proof-causal-controls-v1', 'result': 'PASS',
              'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_pins': {name: pin(ROOT / name) for name in sorted({case[1] for case in cases} | {
                  'tests/stream_bridge_native_runtime_test.cpp', 'tests/test_stream_bridge_hevc_coverage.cpp',
                  'tests/stream_bridge_hevc_fixture.h', 'tests/check_hevc_coverage_gates.py',
                  'bridge/stream_bridge_hevc_coverage.cpp', 'bridge/stream_bridge_hevc_coverage.h'})},
              'hardware_verified': False, 'device_actions': False, 'results': results}
    (output / 'results.json').write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    print('4 causal HEVC proof removals each fail their exact production witness')

if __name__ == '__main__':
    main()
