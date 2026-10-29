import importlib.util,io,json,tempfile,unittest,zipfile,subprocess,shutil
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('probe_analysis',ROOT/'tools/analyze_nvenc_quality_probe.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class Tests(unittest.TestCase):
    def test_fractional_and_endpoint_errors_have_separate_denominators(self):
        result=p.quality(np.array([0,0,255,255,64,128],dtype=np.uint8),np.array([0,3,254,255,62,130],dtype=np.uint8))
        self.assertEqual((result['zero_samples'],result['zero_changed'],result['full_samples'],result['full_changed']),(2,1,2,1))
        self.assertEqual(result['fractional_MAE'],2);self.assertEqual(result['absolute_max'],3)
    def test_lossless_identity_all_256_values(self):
        data=np.arange(256,dtype=np.uint8);result=p.quality(data,data)
        self.assertEqual((result['absolute_MAE'],result['absolute_max'],result['zero_changed'],result['full_changed']),(0,0,0,0))
    def test_wrong_samples_refused(self):
        for a,b in [(np.zeros(2,dtype=np.uint8),np.zeros(1,dtype=np.uint8)),(np.zeros(2),np.zeros(2)),(np.zeros(0,dtype=np.uint8),np.zeros(0,dtype=np.uint8))]:
            with self.assertRaises(ValueError):p.quality(a,b)
    def archive(self,names):
        tmp=tempfile.NamedTemporaryFile(suffix='.zip');data=io.BytesIO()
        with zipfile.ZipFile(data,'w') as z:
            for name in names:z.writestr(name,b'{}')
        Path(tmp.name).write_bytes(data.getvalue());self.addCleanup(tmp.close);return Path(tmp.name)
    def test_zip_parent_and_nested_paths_refused(self):
        for name in ['../probe.json','dir/probe.json','dir\\probe.json','..']:
            with self.assertRaises(ValueError):p.archive_files(self.archive([name]))
    def test_duplicate_and_file_limits_refused(self):
        with self.assertWarns(UserWarning):path=self.archive(['probe.json','probe.json'])
        with self.assertRaises(ValueError):p.archive_files(path)
        with self.assertRaises(ValueError):p.archive_files(self.archive(['file%d.json'%i for i in range(65)]))
    @unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('ffprobe'),'requires offline FFmpeg CLI')
    def test_real_lossless_hevc_prefix_keeps_direct_full_range_and_last_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=np.tile(np.arange(64,dtype=np.uint8)*4,(64,1));frames=[]
            for i in range(8):
                y=(source.astype(np.uint16)+i).clip(0,255).astype(np.uint8)
                frames.append(y.tobytes()+bytes([128])*(64*64//2))
            raw=root/'source.yuv';raw.write_bytes(b''.join(frames));clip=root/'prefix.hevc'
            command=['ffmpeg','-v','error','-f','rawvideo','-pix_fmt','yuvj420p','-s','64x64','-r','90','-i',str(raw),
                     '-frames:v','8','-c:v','libx265','-preset','ultrafast','-x265-params',
                     'log-level=error:pools=1:frame-threads=1:lossless=1:repeat-headers=1:bframes=0:keyint=8:min-keyint=8:scenecut=0:range=full:info=0',str(clip)]
            run=subprocess.run(command,capture_output=True,timeout=30)
            if run.returncode and b'Unknown encoder' in run.stderr:self.skipTest('FFmpeg lacks owned CPU x265 fixture encoder')
            self.assertEqual(run.returncode,0,run.stderr.decode(errors='replace'))
            decoded,count,stream=p.decode_last(clip.read_bytes(),64,64)
            self.assertEqual(count,8);self.assertEqual(stream['color_range'],'pc')
            expected=(source.astype(np.uint16)+7).clip(0,255).astype(np.uint8).ravel()
            self.assertEqual(p.quality(expected,decoded)['absolute_max'],0)
    def test_flat_diagnostic_archive_is_read_exactly(self):
        self.assertEqual(p.archive_files(self.archive(['probe.json','reference-last.r8'])),{'probe.json':b'{}','reference-last.r8':b'{}'})
if __name__=='__main__':unittest.main()
