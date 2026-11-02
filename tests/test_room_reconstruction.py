"""PC job boundaries, calibration retention and owned-process cancellation."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from room_reconstruction import backend as b, fixture


class PcReconstructionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def dataset(self):
        return fixture.create(self.root/'source',width=16,height=16,views=2)

    def test_nvidia_profiles_use_available_memory_and_do_not_require_a_4090(self):
        devices = b.parse_nvidia_gpus('0, NVIDIA GeForce RTX 2060, 6144, 4096, GPU-abcd\n')
        self.assertEqual('compact',b.choose_profile(devices[0]).name)
        busy = b.NvidiaGpu(0,'NVIDIA GeForce RTX 4090',24564,2500,'GPU-busy')
        with self.assertRaises(b.ReconstructionError):
            b.choose_profile(busy)
        self.assertEqual('detail',b.choose_profile(b.NvidiaGpu(0,'RTX 4090',24564,22000,'GPU-free')).name)
        for bad in ('','0, GPU, N/A, 0, GPU-x','0, GPU, 1024, 2048, GPU-x'):
            with self.assertRaises(b.ReconstructionError):
                b.parse_nvidia_gpus(bad)

    def test_snapshot_preserves_independent_metric_camera_pose_and_original_files(self):
        source = self.dataset()
        metadata = b.read_json(source/'transforms.json')
        expected = [[0.,0.,1.,1.2],[0.,1.,0.,1.6],[-1.,0.,0.,-2.3],[0.,0.,0.,1.]]
        metadata['frames'][0]['transform_matrix'] = expected
        (source/'transforms.json').write_text(json.dumps(metadata))
        before = {str(p.relative_to(source)):p.read_bytes() for p in source.rglob('*') if p.is_file()}
        result = b.snapshot_dataset(source,self.root/'snapshot')
        self.assertEqual(2,result['images'])
        self.assertEqual(expected,b.read_json(self.root/'snapshot/transforms.json')['frames'][0]['transform_matrix'])
        self.assertEqual(before,{str(p.relative_to(source)):p.read_bytes() for p in source.rglob('*') if p.is_file()})

    def test_bad_geometry_and_escaping_paths_cannot_reach_the_trainer(self):
        source = self.dataset()
        original = b.read_json(source/'transforms.json')
        for index in range(3):
            value = json.loads(json.dumps(original))
            if index == 0:
                value['frames'][0]['transform_matrix'][0][0] *= -1
                value['frames'][0]['transform_matrix'][2][0] *= -1
            elif index == 1:
                value['frames'][0]['file_path'] = '../outside.png'
            else:
                value['w'] = 32
            (source/'transforms.json').write_text(json.dumps(value))
            target = self.root/f'refused-{index}'
            with self.assertRaises(b.ReconstructionError):
                b.snapshot_dataset(source,target)
            self.assertFalse(target.exists())

    def test_existing_output_is_never_replaced(self):
        source = self.dataset()
        target = self.root/'existing'
        target.mkdir()
        (target/'keep').write_text('accepted data')
        with self.assertRaises(FileExistsError):
            b.snapshot_dataset(source,target)
        self.assertEqual('accepted data',(target/'keep').read_text())

    def test_metric_frame_refuses_scaled_shifted_or_mirrored_models(self):
        value = {'format':'spirula-scene-transform','version':1,
                 'train_from_world':{'scale':1,'matrix_4x4':[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]},
                 'world_from_train':{'scale':1,'matrix_4x4':[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]}}
        (self.root/'scene_transform.json').write_text(json.dumps(value))
        b.require_metric_frame(self.root)
        for change in ('scale','shift','mirror'):
            altered = json.loads(json.dumps(value))
            if change == 'scale':
                altered['train_from_world']['scale'] = 2
            elif change == 'shift':
                altered['world_from_train']['matrix_4x4'][1][3] = .1
            else:
                altered['world_from_train']['matrix_4x4'][2][2] = -1
            (self.root/'scene_transform.json').write_text(json.dumps(altered))
            with self.assertRaises(b.ReconstructionError):
                b.require_metric_frame(self.root)

    def test_real_child_failure_keeps_log_and_completed_checkpoint(self):
        checkpoint = self.root/'accepted.ckpt'
        checkpoint.write_text('keep')
        with self.assertRaises(b.ReconstructionError):
            b.run_owned([sys.executable,'-c','print("controlled GPU failure");raise SystemExit(13)'],self.root/'failure.log')
        self.assertIn('controlled GPU failure',(self.root/'failure.log').read_text())
        self.assertEqual('keep',checkpoint.read_text())

    def test_real_owned_child_stop_does_not_remove_checkpoint(self):
        checkpoint = self.root/'accepted.ckpt'
        checkpoint.write_text('keep')
        stop = threading.Event()
        timer = threading.Timer(.25,stop.set)
        timer.start()
        self.addCleanup(timer.join)
        with self.assertRaisesRegex(b.ReconstructionError,'stopped'):
            b.run_owned([sys.executable,'-c','import time;print("running",flush=True);time.sleep(60)'],self.root/'stop.log',stop)
        self.assertEqual('keep',checkpoint.read_text())
        self.assertIn('running',(self.root/'stop.log').read_text())

    def test_native_job_preserves_metric_units_and_has_no_network_viewer(self):
        profile = b.Profile('compact',200000,640)
        args = b.train_arguments(Path('trainer.exe'),Path('dataset'),Path('outputs/job'),profile,2000,2464)
        for flag,expected in (('--train-frame','points'),('--scene-center','none'),('--auto-scale-poses','0'),
                              ('--relative-scale','1'),('--disable-viewer','1'),('--keep-viewer-alive','0'),
                              ('--train-resolution-divisor','4'),('--device','NVIDIA')):
            self.assertEqual(expected,args[args.index(flag)+1])
        self.assertNotIn('shell',args)


if __name__ == '__main__':
    unittest.main()
