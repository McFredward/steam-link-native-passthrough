"""Public cross-language contract; no private native shader inputs required."""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ArtifactPins(unittest.TestCase):
    def test_installer_admits_both_actual_live_transform_outputs(self):
        # The old artifact header describes rollback readers. It no longer
        # describes what the generator and active native renderer produce.
        header = (ROOT / 'layer/stream_bridge_coverage_transform_data.h').read_text()
        tree = ast.parse((ROOT / 'setup/stream_bridge_install.py').read_text())
        assignments = {target.id: ast.literal_eval(node.value) for node in tree.body
                       if isinstance(node, ast.Assign) for target in node.targets
                       if isinstance(target, ast.Name) and target.id in ('GENERATED', 'GENERATED_CURRENT')}
        for profile, constant in (('Stable', 'GENERATED'), ('Current', 'GENERATED_CURRENT')):
            native = {}
            for name, filename in (('Coverage', 'coverage.spv'), ('Camera', 'camera-only.spv')):
                program = re.search(r'\bProgram ' + profile + name + r'\{[^\n]*Words,(\d+)u,\d+u,"([a-f0-9]{64})"\};', header)
                self.assertIsNotNone(program)
                native[filename] = {'bytes': int(program[1]), 'sha256': program[2]}
            self.assertEqual(assignments[constant], native, profile)

    def test_product_runtime_has_no_atlas_decoder_selection(self):
        for name in ('layer/stream_bridge_renderer.cpp', 'layer/stream_bridge_process.cpp',
                     'layer/stream_bridge_process.h', 'layer/stream_bridge_artifacts.h'):
            text = (ROOT / name).read_text()
            for forbidden in ('readLegacyFragmentCold', 'legacyWords', 'LegacyBytes',
                              'LegacySha256', 'legacy.spv', 'entry->legacy'):
                self.assertNotIn(forbidden, text, name)
        tree = ast.parse((ROOT / 'tools/native_fragment_stream_coverage.py').read_text())
        imported = {item.name for node in tree.body if isinstance(node, ast.Import) for item in node.names}
        self.assertIn('native_fragment_count1_interface', imported)
        self.assertFalse(imported & {'native_fragment_scene_presenter', 'native_fragment_packed_output',
                                    'native_fragment_atlas_probe'})


if __name__ == '__main__':
    unittest.main()
