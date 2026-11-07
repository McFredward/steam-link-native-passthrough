#!/usr/bin/env python3
"""Record bounded native timing/sensor diagnostics without requesting images.

The manually started reader must already be active. This tool never starts a
reader, configures a camera or changes a service. It parks its own interest on
exit; the session owner remains responsible for stopping the manual reader.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import time

from probe.room_camera_api_client import Client


def open_private_output(path):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0)
    descriptor = os.open(path, flags, 0o600)
    return os.fdopen(descriptor, 'w', encoding='utf-8')


def capture(client, address, output, *, seconds=12, maximum=1800, clock=time.monotonic, pause=time.sleep):
    if type(seconds) not in (int, float) or not 1 <= seconds <= 30 or type(maximum) is not int or not 4 <= maximum <= 1800:
        raise ValueError('Metadata diagnostic exceeds its duration or record budget')
    written = 0

    def write(value):
        nonlocal written
        text = json.dumps(value, separators=(',', ':'))+'\n'
        written += len(text.encode('utf-8'))
        if written > 32*1024*1024:
            raise ValueError('Metadata diagnostic exceeds its private output budget')
        output.write(text)

    def request(command, **values):
        metadata, pixels = client.request(command, **values)
        if pixels:
            raise ValueError('Metadata diagnostic unexpectedly received image data')
        return metadata

    def describe():
        value = request('describe')
        if (value.get('schema') != 'sfnp-native-camera-api-v1'
                or value.get('diagnostic_metadata_available') is not True
                or value.get('setup_privileges_dropped') is not True or value.get('maps_read_only') is not True
                or value.get('camera_settings_changed') is not False or value.get('boot_changes') is not False
                or value.get('pixel_reads_active') is not False or value.get('high_resolution_reads_active') is not False):
            raise ValueError('A checked reader with inactive pixel consumers is required')
        return value

    first = describe()
    identity = (address, first['producer_pid'], first['producer_start_ticks'])
    sensors = {v['sensor']: v['role'] for v in first['streams']}
    if len(sensors) != 2 or set(sensors.values()) != {'passthrough_left', 'passthrough_right'}:
        raise ValueError('Two distinct native Arcturus identities are required')
    published = first.get('counters', {}).get('published', 0)
    write({'schema': 'sfnp-room-metadata-diagnostic-v1', 'kind': 'start', 'description': first,
           'this_probe_requests_images': False})
    after = count = 0; sequences = {}; counts = {}; sequence_gaps = 0; started = checked = clock()
    try:
        while count < maximum and clock()-started < seconds:
            value = request('metadata_next', after=after)
            if value.get('status') == 'no-fresh-metadata':
                if 'resume_after' in value:
                    raise ValueError('Metadata observation expired before it was read')
                pause(.002)
            elif value.get('status') == 'metadata':
                token = value.get('observation_token')
                if (value.get('schema') != 'sfnp-native-camera-metadata-observation-v1'
                        or type(token) is not int or token != after+1
                        or value.get('contains_image_data') is not False or value.get('room_scan_ready') is not False):
                    raise ValueError('Metadata cursor or diagnostic envelope changed')
                event = value['metadata']; timing = event.get('capture_metadata')
                if (event.get('schema') != 'sfnp-native-camera-metadata-v1'
                        or (event.get('source_session'), event.get('producer_pid'), event.get('producer_start_ticks')) != identity
                        or event.get('sensor') not in sensors or sensors[event['sensor']] != event.get('role')
                        or event.get('kernel_role_verified') is not True or event.get('queue_mapping_exact') is not True
                        or event.get('metadata_copy_stable') is not True or event.get('pixel_copy_requested') is not False
                        or event.get('contains_image_data') is not False or not isinstance(timing, dict)
                        or timing.get('capture_clock') != 'CLOCK_MONOTONIC_RAW' or timing.get('v4l_timestamp_matches') is not True):
                    raise ValueError('Native metadata source, eye or copy proof changed')
                payload = timing.get('sensor_metadata_hex')
                if not isinstance(payload, str) or len(payload) != 2416 or len(bytes.fromhex(payload)) != 1208:
                    raise ValueError('Embedded sensor metadata extent changed')
                if (timing['eof_isr_raw_ns']//1000*1000 != event['capture_ticks']
                        or not 0 < timing['sof_raw_ns'] <= timing['eof_raw_ns'] <= timing['eof_isr_raw_ns']
                        or not 0 < event['metadata_copy_begin_ns'] <= event['metadata_copy_end_ns']):
                    raise ValueError('Native frame clock binding changed')
                sequence = event.get('sequence'); previous = sequences.get(event['sensor'])
                if type(sequence) is not int or sequence <= 0 or previous is not None and sequence <= previous:
                    raise ValueError('Native metadata sequence did not advance')
                if previous is not None:
                    sequence_gaps += sequence-previous-1
                sequences[event['sensor']] = sequence; after = token; count += 1
                counts[event['sensor']] = counts.get(event['sensor'], 0)+1
                write(value)
            else:
                raise ValueError('Metadata history unavailable: '+str(value.get('status')))
            if clock()-checked >= .5:
                current = describe()
                if current.get('counters', {}).get('published', 0) != published:
                    raise ValueError('Another consumer copied pixels during the metadata diagnostic')
                checked = clock()
        final = describe()
        if (final.get('counters', {}).get('published', 0) != published
                or (address, final['producer_pid'], final['producer_start_ticks']) != identity):
            raise ValueError('The reader copied pixels or changed producer during the diagnostic')
        summary = {'kind': 'complete', 'records': count, 'records_by_sensor': counts,
                   'native_sequence_gaps': sequence_gaps, 'elapsed_seconds': clock()-started,
                   'pixel_copies_during_diagnostic': 0, 'this_probe_requests_images': False,
                   'room_scan_ready': False}
        if len(sequences) != 2 or count < 4:
            raise ValueError('Insufficient metadata from both native cameras')
        write(summary)
        return summary
    finally:
        try:
            request('park')
        except (OSError, ValueError):
            pass  # Closing the owned socket also releases this client's interest.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('address')
    parser.add_argument('output', type=Path)
    parser.add_argument('--seconds', type=float, default=12)
    args = parser.parse_args()
    client = Client(args.address)
    try:
        with open_private_output(args.output) as output:
            result = capture(client, args.address, output, seconds=args.seconds)
        print(json.dumps(result), flush=True)
    finally:
        client.close()


if __name__ == '__main__':
    main()
