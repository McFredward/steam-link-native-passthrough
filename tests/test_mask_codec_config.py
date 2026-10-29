"""Exercise persistent codec preferences without an installation or device."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sfnp_mask_config',ROOT/'tools/configure_mask_codec.py')
config=importlib.util.module_from_spec(spec);spec.loader.exec_module(config)


class Preferences(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix='sfnp-mask-config-')
        self.addCleanup(temporary.cleanup);self.root=Path(temporary.name)
        self.path=self.root/'mask-codec.conf'

    def test_disable_persists_with_no_prior_preference(self):
        config.configure(self.root)
        self.assertEqual(self.path.read_bytes(),b'SFNP_MASK_HEVC_V1\n0\n')
        config.configure(self.root)
        self.assertEqual(self.path.read_bytes(),b'SFNP_MASK_HEVC_V1\n0\n')

    def test_disable_replaces_enabled_preference_and_can_be_reenabled(self):
        config.configure(self.root,35000000)
        self.assertEqual(self.path.read_bytes(),b'SFNP_MASK_HEVC_V1\n35000000\n')
        config.configure(self.root)
        self.assertEqual(self.path.read_bytes(),b'SFNP_MASK_HEVC_V1\n0\n')
        config.configure(self.root,20000000)
        self.assertEqual(self.path.read_bytes(),b'SFNP_MASK_HEVC_V1\n20000000\n')
        self.assertFalse((self.root/'mask-codec.tmp').exists())

    def test_unrelated_config_and_staging_are_preserved(self):
        self.path.write_bytes(b'unrelated')
        with self.assertRaises(RuntimeError):config.configure(self.root)
        self.assertEqual(self.path.read_bytes(),b'unrelated')
        self.path.unlink();staging=self.root/'mask-codec.tmp';staging.write_bytes(b'owned elsewhere')
        with self.assertRaises(RuntimeError):config.configure(self.root)
        self.assertEqual(staging.read_bytes(),b'owned elsewhere')
        self.assertFalse(self.path.exists())

    def test_missing_installation_is_preserved(self):
        with self.assertRaises(RuntimeError):config.configure(self.root/'missing')
        self.assertFalse((self.root/'missing').exists())


if __name__=='__main__':unittest.main()
