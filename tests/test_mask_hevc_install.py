"""Real private decoder install/undo on isolated fixture files, no device calls."""
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('hevc_install_fixtures',ROOT/'tests/test_stream_bridge_install.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
s,b=fixture.s,fixture.b


class Tests(unittest.TestCase):
    def setUp(self):
        self.f=fixture.Tests('runTest');self.addCleanup(self.f.doCleanups);self.f.setUp()

    def add_decoder(self,names=None):
        f=self.f
        for name in names or b.MASK_CODEC_FILES:
            raw=f.bundle.files['frame/'+b.LIBRARY]+name.encode()
            s.put(f.bundle_root/name,raw)
            f.manifest['files'][name]={'bytes':len(raw),'sha256':s.sha(raw)}
            f.manifest['frame']['files'].append(name)
        path=f.bundle_root/'stream-bridge-bundle.json'
        s.put(path,s.encoded(f.manifest),s.read(path))
        f.bundle=b.Bundle(f.bundle_root,s)

    def test_private_libraries_are_owned_installed_checked_and_undone(self):
        f=self.f;self.add_decoder()
        with f.fake_generation():f.unit.install(f.bundle,{**f.pair,'role':'receiver'},'192.168.1.4',47631)
        for name in b.MASK_CODEC_FILES:
            target=f.froot/Path(name).relative_to('frame')
            self.assertEqual(s.read(target),f.bundle.files[name])
        self.assertTrue(f.unit.inventory(f.bundle)['owned_files_current'])
        target=f.froot/'libavutil-sfnp-mask.so.60'
        original=s.read(target);s.put(target,original+b'corrupt',original)
        self.assertFalse(f.unit.inventory(f.bundle)['owned_files_current'])
        s.put(target,original,s.read(target))
        f.unit.remove()
        for name in b.MASK_CODEC_FILES:self.assertFalse((f.froot/Path(name).relative_to('frame')).exists())

    def test_partial_pair_is_refused_before_any_install(self):
        with self.assertRaises(ValueError):self.add_decoder([next(iter(b.MASK_CODEC_FILES))])
        self.assertFalse(self.f.froot.exists())


if __name__=='__main__':unittest.main()
