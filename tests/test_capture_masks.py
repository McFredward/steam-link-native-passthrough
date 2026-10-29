#!/usr/bin/env python3
"""Cross-language writer/reader checks using real native diagnostic output."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

WRITER, CHECKER = map(Path, sys.argv[1:3])
del sys.argv[1:3]
spec = importlib.util.spec_from_file_location('capture_masks', Path(__file__).parents[1] / 'tools/capture_masks.py')
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureReaderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='sfnp-capture-reader-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        subprocess.run([str(WRITER), '--fixture', str(self.root)], check=True, capture_output=True)
        self.path = self.root / 'mask-capture/latest.sfmc'
        self.data = self.path.read_bytes()

    def records(self, path=None):
        iterator = capture.capture_records(path or self.path)
        rows = []
        while True:
            try:
                rows.append(next(iterator))
            except StopIteration as done:
                return rows, done.value

    def changed(self, data):
        path = self.root / 'changed.sfmc'
        path.write_bytes(data)
        return path

    def rejected(self, data):
        path = self.changed(data)
        with self.assertRaises(RuntimeError):
            self.records(path)
        result = subprocess.run([str(CHECKER), str(path)], capture_output=True)
        self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_actual_writer_preserves_alpha_and_submission_qos(self):
        rows, summary = self.records()
        self.assertEqual(summary['reason'], 'stopped')
        self.assertEqual(summary['frames'], 2)
        self.assertEqual([row['frame'] for row, _ in rows], [41, 42])
        self.assertEqual(rows[0][1], bytes(range(256)))
        self.assertEqual(rows[1][1], bytes([255]) + bytes(range(1, 256)))
        self.assertEqual(rows[0][0]['applied_average'], 1000000)
        self.assertEqual(rows[0][0]['reserved_metadata'], 199000000)
        self.assertEqual(rows[0][0]['qos_flags'], 7)
        self.assertEqual(rows[1][0]['native_input_timestamp'], 123001)
        output = self.root / 'selected.a8'
        result = subprocess.run([str(CHECKER), str(self.path), '--frame', '1', str(output)], check=True, capture_output=True, text=True)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])['frames'], 2)
        self.assertEqual(output.read_bytes(), rows[1][1])
        output.write_bytes(b'personal')
        self.assertNotEqual(subprocess.run([str(CHECKER), str(self.path), '--frame', '1', str(output)], capture_output=True).returncode, 0)
        self.assertEqual(output.read_bytes(), b'personal')

    def test_integrity_bounds_footer_and_truncation(self):
        for offset in (0, 40, 48 + 88, 48 + 176 + 77, len(self.data) - 8):
            with self.subTest(offset=offset):
                bad = bytearray(self.data)
                bad[offset] ^= 1
                self.rejected(bad)
        bad = bytearray(self.data)
        struct.pack_into('<I', bad, len(bad) - 24, 1)
        self.rejected(bad)  # A partial prefix cannot be labelled all128 complete.
        self.rejected(self.data[:-1])
        self.rejected(self.data + b'foreign')
        bad = bytearray(self.data)
        struct.pack_into('<I', bad, 48 + 8, capture.MAX_PAYLOAD + 1)
        self.rejected(bad)

    def test_checksum_does_not_hide_epoch_or_frame_gaps(self):
        second = 48 + 176 + 256
        for offset in (16, 24, 32, 48):
            bad = bytearray(self.data)
            bad[second + offset] ^= 4
            bad[second + 144:second + 176] = hashlib.sha256(bad[second:second + 144] + bad[second + 176:second + 176 + 256]).digest()
            with self.subTest(offset=offset):
                self.rejected(bad)

    def test_request_refuses_stale_and_hardlinked_targets(self):
        request = self.root / 'mask-capture.request'
        request.write_text('older request')
        with self.assertRaisesRegex(RuntimeError, 'request already exists'):
            capture.request(self.root, 0)
        self.assertEqual(request.read_text(), 'older request')
        request.unlink()
        request.hardlink_to(self.path)
        with self.assertRaisesRegex(RuntimeError, 'Linked diagnostic path'):
            capture.request(self.root, 0)
        self.assertEqual(request.read_bytes(), self.data)

    def test_no_raw_extraction_until_whole_file_validates(self):
        bad = self.changed(self.data[:-1])
        target = self.root / 'must-not-exist.a8'
        result = subprocess.run([str(CHECKER), str(bad), '--frame', '0', str(target)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(target.exists())


if __name__ == '__main__':
    unittest.main()
