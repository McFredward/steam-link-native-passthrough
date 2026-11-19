#!/usr/bin/env python3
"""Restore delayed/unproved retry behavior and require the real TCP fixture to reject it."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--library-directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = (root / 'pc/stream-bridge-sender-provider-runtime.cpp').read_text()
    controls = {
        'delayed-first-retry': ('if(provenRetryAvailable_){', 'if(false&&provenRetryAvailable_){',
            'fully proved session permits one immediate fresh listener without advancing the retry clock'),
        'unconsumed-immediate-retry': ('provenRetryAvailable_=false;retryAt_=now();', 'retryAt_=now();',
            'peer authentication without a fresh coded-AU echo cannot rearm the immediate retry'),
        'unproved-rearm': ('if(encoder_.peerConfirmed&&encoder_.probeObserved&&control_.peerVerified())provenRetryAvailable_=true;',
            'if(encoder_.peerConfirmed&&control_.peerVerified())provenRetryAvailable_=true;',
            'peer authentication without a fresh coded-AU echo cannot rearm the immediate retry'),
    }
    results = {}
    with tempfile.TemporaryDirectory(prefix='reconnect-controls-', dir=output) as temporary:
        for name, (old, new, expected) in controls.items():
            if source.count(old) != 1:
                raise RuntimeError('Reconnect causal anchor changed: ' + name)
            implementation = Path(temporary) / (name + '.cpp')
            implementation.write_text(source.replace(old, new))
            binary = Path(temporary) / name
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                       '-pthread', '-I' + str(root), '-I' + str(root / 'pc'), '-isystem', str(args.sdk.resolve()),
                       str(root / 'tests/test_stream_bridge_sender_provider_runtime.cpp'), str(implementation),
                       *[str(args.library_directory.resolve() / lib) for lib in (
                           'libsfnp_stream_sender_boundary.a', 'libsfnp_stream_bridge_control.a', 'libsfnp_stream_bridge.a')],
                       '-o', str(binary)]
            with (output / (name + '-build.log')).open('w') as log:
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=90)
            with (output / (name + '.log')).open('w') as log:
                result = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT, timeout=20)
            body = (output / (name + '.log')).read_text()
            if result.returncode != 1 or expected not in body:
                raise RuntimeError('Reconnect mutation failed for an unrelated reason: ' + name)
            results[name] = {'exit_code': result.returncode, 'expected_failure': expected}
    (output / 'results.json').write_text(json.dumps({
        'runtime_source_sha256': hashlib.sha256(source.encode()).hexdigest(),
        'controls': results, 'device_access': False,
    }, indent=2) + '\n')
    print('All three actual TCP retry mutations fail their intended proof assertion.')


if __name__ == '__main__':
    main()
