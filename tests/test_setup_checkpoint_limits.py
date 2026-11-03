"""Actual recovery files above the ordinary32MiB cap; no device access."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
s=load('checkpoint_limit_engine','setup/steam_link_setup.py')
c=load('checkpoint_limit_helper','setup/steam_link_setup_checkpoint.py')


class RecoveryLimits(unittest.TestCase):
    def test_shipped_room_payload_fresh_update_and_retained_continuation_restore_exactly(self):
        # Use the actual public native binaries and sources, not tiny ELF
        # placeholders. Nested pending/committed receipts reproduce the real
        # bridge-prepare failure even when no single file approaches 32 MiB.
        archive=ROOT/'distribution/steam-link-native-passthrough.zip'
        receipt=s.parse(archive.with_suffix('.zip.receipt.json').read_bytes())
        raw=archive.read_bytes()
        self.assertEqual({'bytes':len(raw),'sha256':s.sha(raw)},receipt['archive'])
        with zipfile.ZipFile(archive) as bundle:
            payload={name:bundle.read(name) for name in bundle.namelist() if name.startswith('frame/')}
        self.assertGreater(sum(map(len,payload.values())),14*1024*1024)
        with tempfile.TemporaryDirectory(prefix='sfnp-production-recovery-') as temporary:
            root=Path(temporary);journal=root/'setup-restore.json';installed=root/'install.json'
            targets={root/name for name in payload}|{installed}
            checkpoint=c.Checkpoint(s,journal,lambda path:path in targets,
                lambda path:c.read_value(s,path),lambda path,old,new:c.replace_value(s,path,old,new))
            def plan(files):
                ops=[{'path':str(root/name),'before':None,'after':c.file_value(data),'mode':0o600}
                     for name,data in files.items()]
                pending=s.encoded({'schema':'production-recovery-fixture','phase':'pending','operations':ops})
                committed=s.encoded({'schema':'production-recovery-fixture','phase':'installed','operations':ops})
                return [checkpoint.operation(root/name,[c.file_value(data)]) for name,data in files.items()]+[
                    checkpoint.operation(installed,[c.file_value(pending),c.file_value(committed)])],committed
            fresh,original_receipt=plan(payload)
            # This is the old refusal, before any package or journal write.
            with mock.patch.object(c,'LIMIT',64*1024*1024):
                with self.assertRaisesRegex(s.SetupError,'exceeds its bound'):checkpoint.begin('install',fresh,[])
            self.assertFalse(journal.exists());self.assertFalse(installed.exists())
            checkpoint.begin('install',fresh,[])
            self.assertGreater(journal.stat().st_size,64*1024*1024)
            for name,data in payload.items():s.put(root/name,data)
            s.put(installed,original_receipt);checkpoint.finish()
            # Different complete payloads force a conservative full update;
            # none of these private test binaries is executed or distributed.
            updated={name:data+b'\nprivate recovery regression\n' for name,data in payload.items()}
            operations,next_receipt=plan(updated)
            checkpoint.begin('update',operations,[])
            self.assertGreater(journal.stat().st_size,100*1024*1024)
            for name,data in updated.items():s.put(root/name,data,payload[name])
            s.put(installed,next_receipt,original_receipt)
            # An interrupted Uninstall can retain this exact current state
            # while transferring Undo authority to the next confirmed Apply.
            checkpoint.retain_current('a'*64)
            self.assertGreater(journal.stat().st_size,140*1024*1024)
            retry,_=plan(updated)
            checkpoint.begin('update',retry,[],target='a'*64)
            checkpoint.restore();checkpoint.verify_restored()
            self.assertEqual(s.read(installed),original_receipt)
            for name,data in payload.items():self.assertEqual(s.read(root/name),data)
            checkpoint.finish(restored=True);self.assertFalse(journal.exists())

    def test_real_large_checkpoint_can_continue_restore_and_finish(self):
        with tempfile.TemporaryDirectory(prefix='sfnp-large-checkpoint-') as temporary:
            root=Path(temporary);target=root/'owned.bin';journal=root/'setup-restore.json'
            before=b'A'*(13*1024*1024);after=b'B'*len(before)
            s.put(target,before)
            checkpoint=c.Checkpoint(s,journal,lambda path:path==target,
                lambda path:c.read_value(s,path),lambda path,old,new:c.replace_value(s,path,old,new))
            operation=checkpoint.operation(target,[c.file_value(after)])
            checkpoint.begin('update',[operation],[])
            self.assertGreater(journal.stat().st_size,s.MAX_FILE)
            self.assertLess(journal.stat().st_size,c.LIMIT)
            with self.assertRaisesRegex(s.SetupError,'Bounded ordinary file'):s.read(journal)
            # This is the failed Apply-with-pending-recovery route from the
            # real43MB journal: retain Undo, replace it, then restore and finish.
            checkpoint.reconcile('a'*64)
            checkpoint.begin('update',[operation],[],target='a'*64)
            s.put(target,after,before)
            checkpoint.restore();self.assertEqual(s.read(target),before)
            checkpoint.finish(restored=True)
            self.assertFalse(journal.exists())
            checkpoint.finish(restored=True)

    def test_explicit_limit_keeps_exact_ownership_and_file_bound(self):
        with tempfile.TemporaryDirectory(prefix='sfnp-checkpoint-identity-') as temporary:
            path=Path(temporary)/'journal.json';s.put(path,b'original')
            with self.assertRaises(s.SetupError):s.put(path,b'next',b'wrong',limit=c.LIMIT)
            with self.assertRaises(s.SetupError):s.remove_owned(path,b'wrong',limit=c.LIMIT)
            self.assertEqual(s.read(path),b'original')
            with self.assertRaisesRegex(s.SetupError,'Bounded ordinary file'):s.remove_owned(path,b'original',limit=2)
            self.assertEqual(s.read(path),b'original')
            s.put(path,b'next',b'original',limit=c.LIMIT)
            s.remove_owned(path,b'next',limit=c.LIMIT);self.assertFalse(path.exists())

    def test_checkpoint_above_its_own_bound_is_preserved(self):
        with tempfile.TemporaryDirectory(prefix='sfnp-oversized-checkpoint-') as temporary:
            path=Path(temporary)/'setup-restore.json'
            with path.open('xb') as stream:stream.truncate(c.LIMIT+1)
            path.chmod(0o600)
            checkpoint=c.Checkpoint(s,path,lambda _:False,lambda _:None,lambda *_:None)
            with self.assertRaisesRegex(s.SetupError,'Bounded ordinary file'):checkpoint.value()
            self.assertEqual(path.stat().st_size,c.LIMIT+1)

    def test_continuation_cannot_grow_a_valid_checkpoint_past_its_bound(self):
        with tempfile.TemporaryDirectory(prefix='sfnp-checkpoint-growth-') as temporary:
            path=Path(temporary)/'setup-restore.json'
            checkpoint=c.Checkpoint(s,path,lambda _:False,lambda _:None,lambda *_:None)
            checkpoint.begin('update',[],[]);original=s.read(path)
            with mock.patch.object(c,'LIMIT',len(original)):
                with self.assertRaisesRegex(s.SetupError,'exceeds its bound'):checkpoint.reconcile('a'*64)
            self.assertEqual(s.read(path),original)


if __name__=='__main__':unittest.main()
