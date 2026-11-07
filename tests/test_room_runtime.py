"""Bounded CUDA wheel extraction and failed-preparation ownership controls."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('room_runtime_test_helper', ROOT/'setup/steam_link_setup_room.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class _RuntimeFixtures(unittest.TestCase):
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
                # ZipInfo normalizes the local separator on Windows. Preserve
                # the exact hostile archive name so both platforms test it.
                member = filename
                if isinstance(filename, str):
                    member = zipfile.ZipInfo('fixture')
                    member.filename = filename
                    member.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(member, data)
        return path


class RuntimeExtractionTests(_RuntimeFixtures):
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
        for number, filename in enumerate(('../outside', '/outside', 'x\\outside', 'C:outside', 'x\0hidden', link)):
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


class SurfaceRuntimePreparationTests(_RuntimeFixtures):
    def surface_manifest(self, *, missing_notice=False):
        members = [(name, ('fixture '+name).encode()) for name in (
            'torch/__init__.py', 'torch/_C.cp314-win_amd64.pyd', 'torch/lib/torch_cuda.dll',
            'numpy/__init__.py', 'torch-2.9.1+cu128.dist-info/LICENSE',
            'torch-2.9.1+cu128.dist-info/NOTICE')]
        if missing_notice:
            members.pop()
        wheel = self.wheel('surface.whl', members)
        manifest = {'schema': 'sfnp-room-surface-runtime-v1', 'python': '3.14',
            'platform': 'win_amd64', 'backend': 'torch-cuda', 'torch_version': '2.9.1+cu128',
            'cuda_version': '12.8', 'wheels': [{'filename': wheel.name}]}
        path = self.root/'surface.json'
        path.write_text(json.dumps(manifest))
        return path, wheel

    def prepare(self, manifest, wheel, cancelled=lambda: False):
        with (mock.patch.object(helper, '_download', return_value=wheel),
              mock.patch.object(helper.shutil, 'disk_usage', return_value=mock.Mock(free=12*1024**3))):
            return helper.prepare_surface_runtime(manifest, self.root/'surface-cache', cancelled)

    def test_reuse_checks_completed_cuda_files_without_loading_them(self):
        manifest, wheel = self.surface_manifest()
        before = ('torch' in sys.modules, 'onnxruntime' in sys.modules)
        runtime = self.prepare(manifest, wheel)
        with mock.patch.object(helper, '_download', side_effect=AssertionError('Unexpected download')):
            self.assertEqual(helper.prepare_surface_runtime(manifest, self.root/'surface-cache'), runtime)
        receipt = json.loads((runtime/'complete.json').read_text())
        self.assertEqual(receipt['schema'], 'sfnp-room-surface-cache-v1')
        self.assertEqual(receipt['manifest'], hashlib.sha256(manifest.read_bytes()).hexdigest())
        self.assertEqual(before, ('torch' in sys.modules, 'onnxruntime' in sys.modules))
        library = runtime/'vendor/torch/lib/torch_cuda.dll'
        library.write_bytes(b'corrupted')
        with self.assertRaisesRegex(ValueError, 'runtime changed'):
            self.prepare(manifest, wheel)
        self.assertEqual(library.read_bytes(), b'corrupted')

    def test_missing_notice_or_cancellation_cannot_publish_or_remove_another_runtime(self):
        cache = self.root/'surface-cache'
        existing = cache/'existing-runtime'
        existing.mkdir(parents=True)
        (existing/'complete.json').write_bytes(b'old receipt')
        (existing/'healthy.dll').write_bytes(b'old library')
        manifest, wheel = self.surface_manifest(missing_notice=True)
        with self.assertRaisesRegex(ValueError, 'missing required'):
            self.prepare(manifest, wheel)
        manifest, wheel = self.surface_manifest()
        with self.assertRaises(InterruptedError):
            self.prepare(manifest, wheel, lambda: True)
        self.assertEqual({path.name for path in cache.iterdir()}, {'existing-runtime', 'downloads'})
        self.assertEqual((existing/'complete.json').read_bytes(), b'old receipt')
        self.assertEqual((existing/'healthy.dll').read_bytes(), b'old library')

    def test_surface_disk_budget_fails_before_download(self):
        manifest, _ = self.surface_manifest()
        with (mock.patch.object(helper, '_download', side_effect=AssertionError('Unexpected download')),
              mock.patch.object(helper.shutil, 'disk_usage', return_value=mock.Mock(free=9*1024**3))):
            with self.assertRaisesRegex(ValueError, '10 GB'):
                helper.prepare_surface_runtime(manifest, self.root/'surface-cache')
        self.assertFalse(any((self.root/'surface-cache').glob('.surface-runtime-*')))

    def test_fresh_child_ignores_inherited_python_and_cuda_search_paths(self):
        sys.path.insert(0, str(ROOT))
        self.addCleanup(sys.path.remove, str(ROOT))
        from room_reconstruction.surface_runtime import warmup_command
        injected = self.root/'injected'
        injected.mkdir()
        (injected/'surface_test_intruder.py').write_text('raise AssertionError("Inherited PYTHONPATH")')
        with mock.patch.dict(os.environ, {'PYTHONPATH': str(injected), 'CUDA_PATH': 'old-cuda',
                'PATH': 'old-ort-dlls', 'SFNP_TEST_MARKER': 'preserved'}):
            command, environment = warmup_command(self.root/'runtime', self.root/'surface.json')
        probe = ('import importlib.util,json,os,sys;print(json.dumps(dict('
                 'isolated=sys.flags.isolated,no_site=sys.flags.no_site,'
                 'intruder=importlib.util.find_spec("surface_test_intruder") is not None,'
                 'marker=os.environ.get("SFNP_TEST_MARKER"),path=os.environ["PATH"])))')
        child = subprocess.run([*command[:5], probe], env=environment, cwd=self.root,
                               capture_output=True, text=True, check=True, timeout=10)
        result = json.loads(child.stdout)
        self.assertEqual((result['isolated'], result['no_site'], result['intruder']), (1, 1, False))
        self.assertEqual(result['marker'], 'preserved')
        self.assertNotIn('old-ort-dlls', result['path'])
        self.assertNotIn('CUDA_PATH', environment)
        self.assertNotIn('--cpu-development', command)
        development, _ = warmup_command(self.root/'runtime', self.root/'surface.json', cpu_development=True)
        self.assertIn('--cpu-development', development)


if __name__ == '__main__':
    unittest.main()
