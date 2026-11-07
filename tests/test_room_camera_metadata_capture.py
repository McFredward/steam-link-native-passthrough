"""The metadata diagnostic cannot silently become a camera-image consumer."""
import copy
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest

from probe.capture_room_metadata import capture, open_private_output


class FixtureClient:
    def __init__(self, fault=None):
        self.fault = fault; self.calls = []; self.count = 0
        self.description = {'schema': 'sfnp-native-camera-api-v1', 'diagnostic_metadata_available': True,
                            'setup_privileges_dropped': True, 'maps_read_only': True,
                            'camera_settings_changed': False, 'boot_changes': False,
                            'pixel_reads_active': False, 'high_resolution_reads_active': False,
                            'producer_pid': 123, 'producer_start_ticks': '456', 'counters': {'published': 0},
                            'streams': [{'sensor': str(i), 'role': role} for i, role in enumerate(
                                ('passthrough_left', 'passthrough_right'))]}

    def request(self, command, **values):
        self.calls.append(command)
        if command == 'describe':
            value = copy.deepcopy(self.description)
            if self.fault == 'interleaved_images' and self.count:
                value['counters']['published'] = 1
            return value, b''
        if command == 'park':
            return {'status': 'parked'}, b''
        if command != 'metadata_next':
            raise AssertionError('The diagnostic requested an image command: '+command)
        if self.fault == 'eviction':
            return {'status': 'metadata-history-evicted', 'resume_after': 2}, b''
        self.count += 1; eye = (self.count-1)%2
        value = {'schema': 'sfnp-native-camera-metadata-observation-v1', 'status': 'metadata',
                 'observation_token': self.count, 'contains_image_data': False, 'room_scan_ready': False,
                 'metadata': {'schema': 'sfnp-native-camera-metadata-v1', 'source_session': 'fixture-address',
                              'producer_pid': 123, 'producer_start_ticks': '456', 'sensor': str(eye),
                              'role': ('passthrough_left', 'passthrough_right')[eye], 'sequence': (self.count+1)//2,
                              'kernel_role_verified': True, 'queue_mapping_exact': True, 'metadata_copy_stable': True,
                              'pixel_copy_requested': False, 'contains_image_data': False, 'capture_ticks': 2000000000,
                              'metadata_copy_begin_ns': 3000000000, 'metadata_copy_end_ns': 3000001000,
                              'capture_metadata': {'capture_clock': 'CLOCK_MONOTONIC_RAW', 'v4l_timestamp_matches': True,
                                                   'sof_raw_ns': 1990000000, 'eof_raw_ns': 1999000000,
                                                   'eof_isr_raw_ns': 2000000789, 'sensor_metadata_hex': '12'*1208}}}
        if self.fault == 'producer':
            value['metadata']['producer_start_ticks'] = '457'
        if self.fault == 'eye':
            value['metadata']['role'] = 'passthrough_right' if eye == 0 else 'passthrough_left'
        if self.fault == 'cursor':
            value['observation_token'] += 1
        if self.fault == 'embedded_extent':
            value['metadata']['capture_metadata']['sensor_metadata_hex'] += 'ff'
        if self.fault == 'clock':
            value['metadata']['capture_ticks'] += 1000
        return value, b'pixels' if self.fault == 'pixels' else b''


class MetadataCaptureTests(unittest.TestCase):
    def test_collects_bounded_diagnostics_without_an_image_command_or_image_payload(self):
        client = FixtureClient(); output = io.StringIO()
        result = capture(client, 'fixture-address', output, maximum=4)
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(result['records'], 4)
        self.assertEqual(result['native_sequence_gaps'], 0)
        self.assertEqual(result['pixel_copies_during_diagnostic'], 0)
        self.assertEqual(len(records), 6)
        self.assertEqual(client.calls, ['describe']+['metadata_next']*4+['describe', 'park'])
        self.assertFalse(records[-1]['room_scan_ready'])

    def test_bad_frame_source_eye_clock_and_cursor_are_retained_as_failures(self):
        for fault in ('producer', 'eye', 'clock', 'cursor', 'embedded_extent', 'pixels', 'eviction', 'interleaved_images'):
            with self.subTest(fault=fault):
                client = FixtureClient(fault); output = io.StringIO()
                self.assertRaises(ValueError, capture, client, 'fixture-address', output, maximum=4)
                self.assertNotIn('"kind":"complete"', output.getvalue())
                self.assertEqual(client.calls[-1], 'park')

    def test_an_existing_image_consumer_is_refused_before_metadata_interest(self):
        client = FixtureClient(); client.description['pixel_reads_active'] = True
        self.assertRaises(ValueError, capture, client, 'fixture-address', io.StringIO(), maximum=4)
        self.assertEqual(client.calls, ['describe'])

    def test_output_is_private_exclusive_and_does_not_follow_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)/'diagnostic.jsonl'
            with open_private_output(target) as output:
                output.write('private fixture\n')
            self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)
            self.assertRaises(FileExistsError, open_private_output, target)
            if os.name != 'nt':
                link = Path(directory)/'link.jsonl'; link.symlink_to(target)
                self.assertRaises(FileExistsError, open_private_output, link)
            self.assertEqual(target.read_text(), 'private fixture\n')

    def test_invalid_limits_do_not_contact_the_reader(self):
        for seconds, maximum in ((0, 4), (31, 4), (True, 4), (1, 1801), (1, False)):
            client = FixtureClient()
            self.assertRaises(ValueError, capture, client, 'fixture-address', io.StringIO(), seconds=seconds, maximum=maximum)
            self.assertEqual(client.calls, [])


if __name__ == '__main__':
    unittest.main()
