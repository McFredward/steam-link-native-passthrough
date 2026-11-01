"""Private decoder source provenance, reuse and failed-patch isolation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mask_ffmpeg_build', ROOT / 'tools/build_mask_hevc_ffmpeg.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class SourcePatches(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='sfnp-source-patch-test-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        prior = builder.ROOT
        builder.ROOT = self.root
        self.addCleanup(setattr, builder, 'ROOT', prior)
        self.source = self.root / 'source'
        self.file = self.source / 'libavcodec/hevc/example.c'
        self.file.parent.mkdir(parents=True)
        self.base, self.changed = b'int value = 1;\n', b'int value = 2;\n'
        self.file.write_bytes(self.base)
        self.patch = self.root / 'change.patch'
        self.patch.write_text('--- a/libavcodec/hevc/example.c\n+++ b/libavcodec/hevc/example.c\n@@ -1 +1 @@\n-int value = 1;\n+int value = 2;\n')
        self.manifest = self.root / 'patches.json'
        self.record = {'schema': 'sfnp-mask-ffmpeg-source-patches-v1', 'patches': [{
            'path': 'change.patch', 'sha256': digest(self.patch.read_bytes()), 'files': {
                'libavcodec/hevc/example.c': {'base_sha256': digest(self.base), 'patched_sha256': digest(self.changed)}}}]}
        self.save()

    def save(self):
        self.manifest.write_text(json.dumps(self.record))

    def test_apply_exact_bytes_and_reuse_without_rewriting(self):
        patches, pins = builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), self.changed)
        self.assertEqual(len(patches), 1)
        self.assertEqual(pins['libavcodec/hevc/example.c'], digest(self.changed))
        timestamp = self.file.stat().st_mtime_ns
        self.assertEqual(builder.source_patches(self.source, self.manifest), (patches, pins))
        self.assertEqual(self.file.stat().st_mtime_ns, timestamp)

    def add_second_patch(self, predecessor=None):
        patch = self.root / 'second.patch'
        patch.write_text('--- a/libavcodec/hevc/example.c\n+++ b/libavcodec/hevc/example.c\n@@ -1 +1 @@\n-int value = 2;\n+int value = 3;\n')
        self.record['patches'].append({'path': 'second.patch', 'sha256': digest(patch.read_bytes()),
            'files': {'libavcodec/hevc/example.c': {'base_sha256': predecessor or digest(self.changed),
                                                 'patched_sha256': digest(b'int value = 3;\n')}}})
        self.save()
        return patch

    def test_ordered_overlapping_patches_and_final_cache_reuse(self):
        self.add_second_patch()
        result = builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), b'int value = 3;\n')
        self.assertEqual(len(result[0]), 2)
        self.assertEqual(builder.source_patches(self.source, self.manifest), result)

    def test_incoherent_patch_predecessor_is_refused_before_replacement(self):
        self.add_second_patch('0' * 64)
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), self.base)

    def test_intermediate_cache_cannot_masquerade_as_final_output(self):
        builder.source_patches(self.source, self.manifest)
        self.add_second_patch()
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), self.changed)

    def test_failed_second_patch_keeps_original_cache(self):
        patch = self.add_second_patch()
        patch.write_bytes(patch.read_bytes() + b'@@ -3 +3 @@\n-missing\n+bad\n')
        self.record['patches'][1]['sha256'] = digest(patch.read_bytes())
        self.save()
        with self.assertRaises(subprocess.CalledProcessError):
            builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), self.base)

    def test_explicit_upstream_baseline_does_not_apply_backport(self):
        self.assertEqual(builder.source_patches(self.source, self.manifest, False), ([], {}))
        self.assertEqual(self.file.read_bytes(), self.base)

    def test_patched_output_cannot_masquerade_as_upstream_baseline(self):
        builder.source_patches(self.source, self.manifest)
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest, False)

    def test_edited_source_is_not_a_trusted_reused_extraction(self):
        self.file.write_bytes(self.base + b'/* edited */')
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest)

    def test_edited_patch_is_refused_before_source_replacement(self):
        self.patch.write_bytes(self.patch.read_bytes() + b'\n')
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), self.base)

    def test_wrong_result_pin_does_not_poison_source_cache(self):
        self.record['patches'][0]['files']['libavcodec/hevc/example.c']['patched_sha256'] = '0' * 64
        self.save()
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), self.base)

    def test_failed_later_hunk_does_not_partly_replace_source(self):
        self.patch.write_bytes(self.patch.read_bytes() + b'@@ -3 +3 @@\n-missing\n+bad\n')
        self.record['patches'][0]['sha256'] = digest(self.patch.read_bytes())
        self.save()
        with self.assertRaises(subprocess.CalledProcessError):
            builder.source_patches(self.source, self.manifest)
        self.assertEqual(self.file.read_bytes(), self.base)

    def test_source_path_escape_is_refused(self):
        self.record['patches'][0]['files'] = {'libavcodec/hevc/../../../outside.c': {
            'base_sha256': digest(self.base), 'patched_sha256': digest(self.changed)}}
        self.save()
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest)

    def test_unknown_patch_schema_is_refused(self):
        self.record['schema'] = 'unknown'
        self.save()
        with self.assertRaises(RuntimeError):
            builder.source_patches(self.source, self.manifest)


if __name__ == '__main__':
    unittest.main()
