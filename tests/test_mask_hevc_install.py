"""Real private decoder install/undo on isolated fixture files, no device calls."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock

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
        f.unit.tx.commit()
        for name in b.MASK_CODEC_FILES:
            target=f.froot/Path(name).relative_to('frame')
            self.assertEqual(s.read(target),f.bundle.files[name])
        self.assertTrue(f.unit.inventory(f.bundle)['owned_files_current'])
        target=f.froot/'libavutil-sfnp-mask.so.60'
        original=s.read(target);s.put(target,original+b'corrupt',original)
        self.assertFalse(f.unit.inventory(f.bundle)['owned_files_current'])
        s.put(target,original,s.read(target))
        f.unit.undo()
        for name in b.MASK_CODEC_FILES:self.assertFalse((f.froot/Path(name).relative_to('frame')).exists())

    def test_partial_pair_is_refused_before_any_install(self):
        with self.assertRaises(ValueError):self.add_decoder([next(iter(b.MASK_CODEC_FILES))])
        self.assertFalse(self.f.froot.exists())


    def remove_decoder_from_target(self):
        f=self.f
        for name in b.MASK_CODEC_FILES:
            f.manifest['files'].pop(name)
            f.manifest['frame']['files'].remove(name)
            s.remove_owned(f.bundle_root/name,s.read(f.bundle_root/name))
        path=f.bundle_root/'stream-bridge-bundle.json'
        s.put(path,s.encoded(f.manifest),s.read(path))
        f.bundle=b.Bundle(f.bundle_root,s)

    def assert_pair_absent(self):
        for name in b.MASK_CODEC_FILES:
            self.assertFalse((self.f.froot/Path(name).relative_to('frame')).exists())

    def test_remote_read_only_inventory_handles_complete_optional_pair(self):
        f=self.f;self.add_decoder();f.install_component_fixture()
        view=b.ReadOnlyView(s,f.bundle.value)
        self.assertFalse(hasattr(view,'files'))
        inventory=f.unit.inventory(view)
        self.assertTrue(inventory['patch_payload_current'])
        self.assertTrue(inventory['component_files_current'])
        self.assertTrue(b.frame_component_current(f.bundle,inventory))

    def test_pair_withdrawal_blocks_pc_only_even_at_same_frame_version(self):
        f=self.f;self.add_decoder();installer=f.install_component_fixture()
        before=f.unit.inventory(f.bundle)
        with mock.patch.object(b,'local_address',return_value='192.168.1.4'):
            self.assertTrue(b.pc_only_candidate(f.bundle,installer.state(),before,'192.168.1.9'))
        self.remove_decoder_from_target()
        view=b.ReadOnlyView(s,f.bundle.value)
        inventory=f.unit.inventory(view)
        self.assertTrue(inventory['owned_files_current'])
        self.assertEqual(inventory['installed_version'],f.bundle.frame['version'])
        self.assertFalse(inventory['patch_payload_current'])
        self.assertFalse(inventory['component_files_current'])
        self.assertFalse(b.frame_component_current(f.bundle,inventory))
        self.assertFalse(b.pc_only_candidate(f.bundle,installer.state(),inventory,'192.168.1.9'))

    def test_active_pair_withdrawal_is_checkpointed_and_undo_restores_both(self):
        f=self.f;self.add_decoder();f.install_component_fixture()
        original_receipt=s.read(f.unit.tx.path)
        original={f.froot/Path(name).relative_to('frame'):f.bundle.files[name] for name in b.MASK_CODEC_FILES}
        self.remove_decoder_from_target()
        with f.fake_generation():f.unit.install(f.bundle,{**f.pair,'role':'receiver'},'192.168.1.4',47631,backup=True,live=True)
        self.assert_pair_absent()
        state=f.unit.tx.state()
        for path in original:
            operation=next(item for item in state['operations'] if item['path']==str(path))
            self.assertIsNone(operation['before']);self.assertIsNone(operation['after'])
        checkpoint=f.unit.checkpoint();saved=checkpoint.value()
        paths={item['path'] for item in saved['operations']}
        self.assertTrue({str(path) for path in original}<=paths)
        checkpoint.restore();checkpoint.verify_restored()
        self.assertEqual(s.read(f.unit.tx.path),original_receipt)
        for path,raw in original.items():self.assertEqual(s.read(path),raw)

    def test_committed_pair_withdrawal_does_not_reinstall_old_libraries_on_uninstall(self):
        f=self.f;self.add_decoder();f.install_component_fixture();self.remove_decoder_from_target()
        with f.fake_generation():f.unit.install(f.bundle,{**f.pair,'role':'receiver'},'192.168.1.4',47631,backup=True,live=True)
        f.unit.tx.commit();f.unit.checkpoint().finish()
        self.assert_pair_absent()
        inventory=f.unit.inventory(b.ReadOnlyView(s,f.bundle.value))
        self.assertTrue(inventory['patch_payload_current']);self.assertTrue(inventory['component_files_current'])
        f.unit.undo()
        self.assert_pair_absent();self.assertFalse(f.unit.tx.path.exists())

    def test_inactive_pair_withdrawal_retains_exact_update_undo(self):
        f=self.f;self.add_decoder();f.mock_shader_pin()
        with mock.patch.object(b,'runtime_admission',return_value=False):
            f.unit.install(f.bundle,{**f.pair,'role':'receiver'},'192.168.1.4',47631)
        f.unit.tx.commit();original_receipt=s.read(f.unit.tx.path)
        original={f.froot/Path(name).relative_to('frame'):f.bundle.files[name] for name in b.MASK_CODEC_FILES}
        self.remove_decoder_from_target()
        with mock.patch.object(b,'runtime_admission',return_value=False):
            result=f.unit.install(f.bundle,{**f.pair,'role':'receiver'},'192.168.1.4',47631,backup=True,live=True)
        self.assertEqual(result['activation_mode'],'inactive');self.assert_pair_absent()
        f.unit.checkpoint().restore();f.unit.checkpoint().verify_restored()
        self.assertEqual(s.read(f.unit.tx.path),original_receipt)
        for path,raw in original.items():self.assertEqual(s.read(path),raw)

    def test_interrupted_pair_withdrawal_restores_both_predecessors(self):
        f=self.f;self.add_decoder();f.install_component_fixture()
        original_receipt=s.read(f.unit.tx.path)
        original={f.froot/Path(name).relative_to('frame'):f.bundle.files[name] for name in b.MASK_CODEC_FILES}
        self.remove_decoder_from_target();replace=f.unit.tx.replace;removed=[]
        def fail_second(operation,*args,**kwargs):
            if operation['path'] in {str(path) for path in original} and operation['after'] is None:
                if removed:raise s.SetupError('fixture second library removal failed')
                removed.append(operation['path'])
            return replace(operation,*args,**kwargs)
        with mock.patch.object(f.unit.tx,'replace',side_effect=fail_second):
            with self.assertRaisesRegex(s.SetupError,'second library removal'):
                with f.fake_generation():f.unit.install(f.bundle,{**f.pair,'role':'receiver'},'192.168.1.4',47631,backup=True,live=True)
        self.assertEqual(len(removed),1)
        self.assertEqual(sum(path.exists() for path in original),1)
        f.unit.checkpoint().restore();f.unit.checkpoint().verify_restored()
        self.assertEqual(s.read(f.unit.tx.path),original_receipt)
        for path,raw in original.items():self.assertEqual(s.read(path),raw)

    def test_missing_receipt_with_remaining_private_libraries_refuses_complete_uninstall(self):
        f=self.f;self.add_decoder()
        original={f.froot/Path(name).relative_to('frame'):f.bundle.files[name] for name in b.MASK_CODEC_FILES}
        for path,raw in original.items():s.put(path,raw)
        with self.assertRaisesRegex(s.SetupError,'receipt is missing'):
            f.unit.undo(purge_legacy=True)
        for path,raw in original.items():self.assertEqual(s.read(path),raw)


if __name__=='__main__':unittest.main()
