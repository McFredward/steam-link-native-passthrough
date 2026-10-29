#!/usr/bin/env python3
"""Request or inspect bounded private PC mask samples. No Steam/Frame changes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import stat
import struct
import sys
import time

HEADER_BYTES, RECORD_BYTES, FOOTER_BYTES = 48, 176, 32
MAX_FRAMES, MAX_BYTES, MAX_PAYLOAD = 128, 48 * 1024**2, 8 * 1024**2
REASONS = ['capturing', 'complete', 'byte-limit', 'queue-busy', 'queue-full',
           'source-changed', 'frame-gap', 'timeout', 'stopped', 'invalid-sample', 'io-error']


def plain_path(path):
    for candidate in [path, *path.parents]:
        try:
            details = candidate.lstat()
        except FileNotFoundError:
            continue
        if (stat.S_ISLNK(details.st_mode) or
                getattr(details, 'st_file_attributes', 0) & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400) or
                stat.S_ISREG(details.st_mode) and details.st_nlink != 1):
            raise RuntimeError(f'Linked diagnostic path is not supported: {candidate}')


def capture_records(path):
    """Validate a finished diagnostic file; yield metadata + compressed payload.

    This format contains no composition authority. Decoding uses the recorded
    native codec/extent; there are no RGB/video bytes or pairing credentials.
    """
    if path.stat().st_size > MAX_BYTES:
        raise RuntimeError('Capture exceeds its maximum size')
    with path.open('rb') as stream:
        def read(size):
            data = stream.read(size)
            if len(data) != size:
                raise RuntimeError('Truncated capture; an interrupted writer did not finish')
            return data
        header = read(HEADER_BYTES)
        if header[:8] != b'SFNPMASK' or struct.unpack_from('<HHIQ', header, 8) != (1, HEADER_BYTES, MAX_FRAMES, MAX_BYTES) or any(header[40:]) or not any(header[24:40]):
            raise RuntimeError('Unsupported diagnostic capture header')
        used, count, previous = HEADER_BYTES, 0, None
        while True:
            tag = read(8)
            if tag == b'SFNPDONE':
                reason, frames, total, last = struct.unpack('<IIQQ', read(FOOTER_BYTES - 8))
                if reason not in range(1, len(REASONS)) or (reason == 1 and frames != MAX_FRAMES) or frames != count or total != used + FOOTER_BYTES or last != (previous['frame'] if previous else 0) or stream.read(1):
                    raise RuntimeError('Capture footer does not match its records')
                return {'token': header[24:40].hex(), 'reason': REASONS[reason], 'frames': frames, 'bytes': total}
            if tag[:4] != b'MSK1' or struct.unpack_from('<I', tag, 4)[0] != RECORD_BYTES:
                raise RuntimeError('Invalid diagnostic record')
            record = tag + read(RECORD_BYTES - 8)
            payload_bytes, encoding = struct.unpack_from('<II', record, 8)
            frame, generation, contract, timestamp = struct.unpack_from('<QQQQ', record, 16)
            pid, width, height, original, complete, encode_us, qos_flags, revision = struct.unpack_from('<IHHIIIIQ', record, 48)
            qos_fields = struct.unpack_from('<12I', record, 80)
            native_timestamp = struct.unpack_from('<Q', record, 128)[0]
            if not 0 < payload_bytes <= MAX_PAYLOAD or encoding not in range(1, 6) or not all((frame, generation, contract, timestamp, pid, width, height, original)) or width * height > 64 * 1024**2 or complete < original or qos_flags > 7 or any(record[136:144]):
                raise RuntimeError('Diagnostic record exceeds its bounds')
            if encoding == 1 and payload_bytes != width * height:
                raise RuntimeError('Raw diagnostic extent mismatch')
            if used + RECORD_BYTES + payload_bytes + FOOTER_BYTES > MAX_BYTES or count >= MAX_FRAMES:
                raise RuntimeError('Diagnostic capture exceeds its limits')
            payload = read(payload_bytes)
            digest = hashlib.sha256(record[:144])
            digest.update(payload)
            if digest.digest() != record[144:]:
                raise RuntimeError('Diagnostic record checksum differs')
            metadata = dict(frame=frame, encoder_generation=generation, source_contract=contract,
                            timestamp_us=timestamp, pid=pid, width=width, height=height,
                            encoding=encoding, payload_bytes=payload_bytes,
                            original_video_bytes=original, complete_au_bytes=complete,
                            added_metadata_bytes=complete-original, encode_us=encode_us,
                            qos_flags=qos_flags, qos_revision=revision,
                            native_input_timestamp=native_timestamp)
            names = ('fps_num', 'fps_den', 'requested_average', 'requested_maximum', 'requested_vbv', 'requested_delay',
                     'applied_average', 'applied_maximum', 'applied_vbv', 'applied_delay', 'reserved_metadata', 'mean_metadata')
            metadata.update(zip(names, qos_fields))
            if previous and (any(metadata[k] != previous[k] for k in ('encoder_generation', 'source_contract', 'pid', 'width', 'height')) or frame != previous['frame'] + 1 or timestamp < previous['timestamp_us']):
                raise RuntimeError('Discontinuous source or actual frame sequence')
            previous = metadata
            count += 1
            used += RECORD_BYTES + payload_bytes
            yield metadata, payload


def inspect(path, per_frame=False):
    rows = []
    records = capture_records(path)
    while True:
        try:
            metadata, _ = next(records)
            if per_frame:
                print(json.dumps(metadata))
            rows.append(metadata)
        except StopIteration as done:
            result = done.value
            break
    if rows:
        elapsed = (rows[-1]['timestamp_us'] - rows[0]['timestamp_us']) / 1e6
        result.update(extent=[rows[0]['width'], rows[0]['height']],
                      first_frame=rows[0]['frame'], last_frame=rows[-1]['frame'],
                      encoded_mask_bytes=sum(row['payload_bytes'] for row in rows),
                      elapsed_seconds=elapsed)
    print(json.dumps(result, indent=2))


def request(root, delay):
    plain_path(root)
    if not root.is_dir():
        raise RuntimeError('The PC companion data directory is missing')
    request_path, status = root / 'mask-capture.request', root / 'mask-capture/status.json'
    plain_path(request_path)
    plain_path(status)
    if request_path.exists():
        raise RuntimeError('A diagnostic request already exists. If its earlier tool has stopped, remove only ' + str(request_path) + ' before retrying.')
    print(f'Capturing up to {MAX_FRAMES} consecutive masks, maximum 48 MiB, in {delay:g} seconds.')
    print('Keep the game running and look at the problematic clouds/effects. Steam and the Frame stay running.')
    time.sleep(delay)
    token = secrets.token_hex(16)
    with request_path.open('x', encoding='ascii') as stream:
        stream.write(f'SFNP_MASK_CAPTURE_V1\n{token}\n')
    deadline = time.monotonic() + 45
    try:
        return wait_capture(request_path, status, root, token, deadline)
    finally:
        # Ctrl+C/timeout cancels this diagnostic lease only, never the game.
        try:
            if request_path.read_text(encoding='ascii') == f'SFNP_MASK_CAPTURE_V1\n{token}\n':
                request_path.unlink()
        except FileNotFoundError:
            pass


def wait_capture(request_path, status, root, token, deadline):
    while time.monotonic() < deadline:
        try:
            state = json.loads(status.read_text(encoding='utf-8'))
            if state.get('token') == token and state.get('state') not in ('starting', 'capturing'):
                print(json.dumps(state, indent=2))
                if not state.get('frames'):
                    raise RuntimeError('No game masks captured: ' + str(state.get('state')))
                target = root / 'mask-capture/latest.sfmc'
                if target.is_file():
                    print(f'Private capture: {target}')
                    inspect(target)
                return
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.2)
    raise RuntimeError('No diagnostic completion. Install the diagnostic PC driver, then retry; no device was restarted.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inspect', type=Path)
    parser.add_argument('--per-frame', action='store_true')
    parser.add_argument('--root', type=Path, help='PC-only diagnostic data root; defaults to LocalAppData')
    parser.add_argument('--delay', type=float, default=10)
    args = parser.parse_args()
    if args.inspect:
        inspect(args.inspect, args.per_frame)
        return
    if not 0 <= args.delay <= 60:
        parser.error('--delay must be between 0 and 60 seconds')
    if args.root is None and not os.environ.get('LOCALAPPDATA'):
        parser.error('Request capture on the Windows PC, or pass a private --root for a fixture')
    request(args.root or Path(os.environ['LOCALAPPDATA']) / 'steam-link-native-passthrough', args.delay)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('Diagnostic request cancelled; the game keeps running.', file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, OSError) as error:
        print(f'Capture failed: {error}', file=sys.stderr)
        sys.exit(1)
