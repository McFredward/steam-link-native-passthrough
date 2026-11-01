import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('bench_build', ROOT / 'tools/build_mask_decode_benchmark.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
BINARY = None
if __name__ == '__main__' and len(sys.argv) > 1 and not sys.argv[1].startswith('-'):
    BINARY = Path(sys.argv.pop(1)).resolve()


class BuilderTest(unittest.TestCase):
    def test_private_instrumentation_has_exact_guarded_sites(self):
        original = (ROOT / 'receiver/stream_bridge_mask_hevc.cpp').read_text()
        instrumented = builder.instrument(original)
        for stage in range(3):
            self.assertEqual(instrumented.count(f'sfnpBenchmarkStage({stage},'), 1)
        self.assertNotIn('AV_CODEC_FLAG_GRAY', instrumented)
        self.assertEqual((ROOT / 'receiver/stream_bridge_mask_hevc.cpp').read_text(), original)
        self.assertIn('AV_CODEC_FLAG_GRAY', builder.instrument(original, True))

    def test_missing_or_ambiguous_instrumentation_site_is_refused(self):
        original = (ROOT / 'receiver/stream_bridge_mask_hevc.cpp').read_text()
        site = 'if(avcodec_send_packet(w.context,w.packet)<0)return Status::DecoderRefused;'
        for text in (original.replace(site, ''), original + site):
            with self.assertRaises(ValueError):
                builder.instrument(text)

    def test_builder_preserves_preexisting_output(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'user-file'
            path.write_bytes(b'keep')
            result = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/build_mask_decode_benchmark.py'),
                '--include', directory, '--output', directory], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('existing files are preserved', result.stderr)
            self.assertEqual(path.read_bytes(), b'keep')


class FixtureBoundsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY:
            raise unittest.SkipTest('Pass the built portable benchmark for real CLI bounds checks')

    def refuse(self, raw, reason, repeats='1'):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input.sfmb'
            path.write_bytes(raw)
            result = subprocess.run([str(BINARY), '/missing-codec', '/missing-util', str(path), repeats],
                                    capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, '')
        self.assertIn(reason, result.stderr)

    def header(self, width=1152, height=4608, count=8):
        return b'SFMB0001' + struct.pack('<HHI', width, height, count)

    def test_bad_magic_or_short_header(self):
        self.refuse(b'SFMB0001', 'invalid offline fixture header')
        self.refuse(b'not-magic' + self.header()[8:], 'invalid offline fixture header')

    def test_unbounded_extent_or_group(self):
        for raw in (self.header(0), self.header(8193), self.header(8192, 8192), self.header(count=9)):
            self.refuse(raw, 'one bounded IPPP8 fixture required')

    def test_missing_truncated_and_oversized_packets(self):
        self.refuse(self.header(), 'missing packet length')
        self.refuse(self.header() + struct.pack('<I', 0), 'packet budget exceeded')
        self.refuse(self.header() + struct.pack('<I', 2**32 - 1), 'packet budget exceeded')
        self.refuse(self.header() + struct.pack('<I', 100) + b'x', 'truncated packet')

    def test_extra_bytes_rejected_before_codec_load(self):
        self.refuse(self.header() + (struct.pack('<I', 1) + b'x') * 8 + b'junk', 'extra fixture bytes')

    def test_repeats_have_fixed_time_and_allocation_bounds(self):
        for repeats in ('0', '9', '-1', '1.5', 'NaN', '999999999'):
            self.refuse(self.header(), 'integer', repeats)


if __name__ == '__main__':
    unittest.main()
