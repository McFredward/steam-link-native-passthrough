"""Bounded CUDA wheel extraction and failed-preparation ownership controls."""
import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('room_runtime_test_helper', ROOT/'setup/steam_link_setup_room.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class RuntimeExtractionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.staging = self.root/'staging'
        self.staging.mkdir()

    def wheel(self, name, members):
        path = self.root/name
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for filename, data in members:
                archive.writestr(filename, data)
        return path

    def test_streaming_large_members_and_identical_namespace_duplicates(self):
        raw = b'cuda-test-data-'*(256*1024)
        wheel = self.wheel('large.whl', [('torch/lib/test.dll', raw), ('nvidia/__init__.py', b'')])
        other = self.wheel('namespace.whl', [('nvidia/__init__.py', b''), ('nvidia/runtime.py', b'pass\n')])
        reads = []
        original = zipfile.ZipExtFile.read
        def bounded(stream, size=-1):
            self.assertGreater(size, 0)
            self.assertLessEqual(size, 1024**2)
            reads.append(size)
            return original(stream, size)
        records = {}
        with (mock.patch.object(zipfile.ZipFile, 'read', side_effect=AssertionError('Whole-member allocation')),
              mock.patch.object(zipfile.ZipExtFile, 'read', bounded)):
            expanded = helper._extract_runtime_wheel(wheel, self.staging, records, 0, lambda: False)
            expanded = helper._extract_runtime_wheel(other, self.staging, records, expanded, lambda: False)
        self.assertGreater(len(reads), 4)
        self.assertEqual(expanded, len(raw)+5)
        self.assertEqual((self.staging/'vendor/torch/lib/test.dll').read_bytes(), raw)
        self.assertEqual(records['vendor/torch/lib/test.dll'],
                         {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
        self.assertEqual(len(records), 3)

    def test_explicit_member_budget_can_support_a_larger_runtime(self):
        raw = b'x'*(2*1024**2)
        wheel = self.wheel('profile.whl', [('torch/lib/test.dll', raw)])
        with self.assertRaisesRegex(ValueError, 'wheel member'):
            helper._extract_runtime_wheel(wheel, self.staging, {}, 0, lambda: False, member_limit=1024**2)
        self.assertEqual(list(self.staging.iterdir()), [])
        records = {}
        helper._extract_runtime_wheel(wheel, self.staging, records, 0, lambda: False,
                                     member_limit=4*1024**2, expanded_limit=4*1024**2)
        self.assertEqual(records['vendor/torch/lib/test.dll']['bytes'], len(raw))

    def test_conflicting_member_does_not_overwrite_a_completed_staged_file(self):
        first = self.wheel('first.whl', [('shared/package.py', b'original')])
        second = self.wheel('second.whl', [('shared/package.py', b'changed')])
        records = {}
        expanded = helper._extract_runtime_wheel(first, self.staging, records, 0, lambda: False)
        expected = dict(records)
        with self.assertRaisesRegex(ValueError, 'Conflicting'):
            helper._extract_runtime_wheel(second, self.staging, records, expanded, lambda: False)
        self.assertEqual((self.staging/'vendor/shared/package.py').read_bytes(), b'original')
        self.assertEqual(records, expected)

    def test_path_symlink_and_expansion_failures_cannot_escape_staging(self):
        link = zipfile.ZipInfo('link')
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for number, filename in enumerate(('../outside', '/outside', 'x\\outside', 'C:outside', link)):
            wheel = self.wheel(f'bad-{number}.whl', [(filename, b'bad')])
            with self.subTest(filename=filename), self.assertRaises(ValueError):
                helper._extract_runtime_wheel(wheel, self.staging, {}, 0, lambda: False)
        budget = self.wheel('budget.whl', [('large.bin', b'x'*100)])
        with self.assertRaisesRegex(ValueError, 'budget'):
            helper._extract_runtime_wheel(budget, self.staging, {}, 0, lambda: False, expanded_limit=99)
        self.assertFalse((self.root/'outside').exists())
        self.assertEqual(list(self.staging.iterdir()), [])

    def test_cancellation_mid_member_cleans_only_owned_preparation(self):
        wheel = self.wheel('cancel.whl', [('torch/lib/test.dll', b'x'*(5*1024**2))])
        model = self.root/'model.onnx'; model.write_bytes(b'model')
        manifest = {'schema': 'sfnp-room-pc-runtime-v1', 'python': '3.14',
                    'wheels': [{'filename': wheel.name}], 'model': {'filename': model.name}, 'licenses': {}}
        manifest_path = self.root/'runtime.json'; manifest_path.write_text(json.dumps(manifest))
        cache = self.root/'cache'
        healthy = cache/'existing-runtime'
        healthy.mkdir(parents=True)
        (healthy/'complete.json').write_bytes(b'existing receipt')
        (healthy/'old.dll').write_bytes(b'healthy old library')
        calls = 0
        def cancelled():
            nonlocal calls
            calls += 1
            return calls >= 4
        inputs = {wheel.name: wheel, model.name: model}
        with mock.patch.object(helper, '_download', side_effect=lambda item, *_: inputs[item['filename']]):
            with self.assertRaisesRegex(InterruptedError, 'extraction cancelled'):
                helper.prepare_runtime(manifest_path, cache, cancelled)
        self.assertEqual((healthy/'complete.json').read_bytes(), b'existing receipt')
        self.assertEqual((healthy/'old.dll').read_bytes(), b'healthy old library')
        self.assertFalse(any(cache.glob('.room-runtime-*')))
        self.assertEqual({p.name for p in cache.iterdir()}, {'existing-runtime', 'downloads'})


if __name__ == '__main__':
    unittest.main()
