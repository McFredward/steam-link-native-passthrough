#!/usr/bin/env python3
"""Offline analysis of owned synthetic NVENC probe data. Never install a runtime.

Requires NumPy and ffmpeg/ffprobe only on the analysis host. GPU bitrate figures
are nominal coded bytes at90Hz; they do not measure network or headset latency.
"""
from __future__ import annotations
import argparse,hashlib,json,re,subprocess,tempfile,zipfile
from pathlib import Path
import numpy as np

MAX_EXPANDED=128*1024*1024

def quality(source,decoded):
    if not source.size or source.shape!=decoded.shape or source.dtype!=np.uint8 or decoded.dtype!=np.uint8:
        raise ValueError('Mismatching nonempty8-bit source/decoded samples')
    error=np.abs(source.astype(np.int16)-decoded.astype(np.int16));fractional=(source>0)&(source<255)
    zero=source==0;full=source==255
    return {'samples':int(source.size),'absolute_MAE':float(error.mean()),'absolute_p99':float(np.percentile(error,99)),
            'absolute_max':int(error.max()),'fractional_samples':int(fractional.sum()),
            'fractional_MAE':float(error[fractional].mean()) if fractional.any() else None,
            'zero_samples':int(zero.sum()),'zero_changed':int(np.count_nonzero(decoded[zero])),
            'full_samples':int(full.sum()),'full_changed':int(np.count_nonzero(decoded[full]!=255))}

def archive_files(path):
    with zipfile.ZipFile(path) as archive:
        infos=archive.infolist()
        if len(infos)>64 or len({i.filename for i in infos})!=len(infos) or sum(i.file_size for i in infos)>MAX_EXPANDED:
            raise ValueError('Probe archive exceeds bounded file/byte limits')
        for info in infos:
            name=info.filename
            if '/' in name or '\\' in name or name in ('.','..') or not re.fullmatch(r'[A-Za-z0-9_.-]+',name):
                raise ValueError('Probe archive must have flat owned filenames')
        return {i.filename:archive.read(i) for i in infos}

def decode_last(encoded,width,height):
    with tempfile.TemporaryDirectory(prefix='sfnp-quality-decode-') as temporary:
        clip=Path(temporary)/'last-gop.hevc';clip.write_bytes(encoded)
        result=subprocess.run(['ffprobe','-v','error','-select_streams','v:0','-show_entries',
            'stream=codec_name,profile,width,height,color_range','-of','json',str(clip)],check=True,capture_output=True,timeout=15)
        streams=json.loads(result.stdout)['streams']
        if len(streams)!=1 or streams[0].get('codec_name')!='hevc' or streams[0].get('width')!=width or streams[0].get('height')!=height or streams[0].get('color_range')!='pc':
            raise ValueError('HEVC sample must have the declared extent and direct full-range luma')
        # Keep native full-range Y. Converting through RGB/limited-range gray
        # would silently change the alpha values being measured.
        result=subprocess.run(['ffmpeg','-v','error','-xerror','-i',str(clip),'-map','0:v:0','-an','-sn','-dn',
            '-frames:v','16','-pix_fmt','yuvj420p','-f','rawvideo','pipe:1'],check=True,capture_output=True,timeout=30)
        frame_bytes=width*height*3//2
        if not result.stdout or len(result.stdout)%frame_bytes or len(result.stdout)>frame_bytes*16:
            raise ValueError('Decoder returned an incomplete or oversized frame sequence')
        return np.frombuffer(result.stdout[-frame_bytes:][:width*height],dtype=np.uint8).copy(),len(result.stdout)//frame_bytes,streams[0]

def analyze(path):
    files=archive_files(path)
    report=json.loads(files['probe.json'])
    if report.get('schema')!='sfnp-nvenc-quality-probe-v1' or report.get('synthetic_input') is not True:
        raise ValueError('Unknown synthetic GPU probe report')
    if report.get('execution_failed') or report.get('completed_cases')!=len(report.get('cases',[])):
        raise ValueError('GPU probe did not complete; inspect stdout.log/stderr.log and probe-status.json first')
    width,height=report['width'],report['height']
    if type(width)is not int or type(height)is not int or width<8 or height<8 or width%2 or height%2 or width>8192 or height>8192 or width*height>8*1024*1024:
        raise ValueError('Invalid probe sample extent')
    source=np.frombuffer(files['reference-last.r8'],dtype=np.uint8)
    if source.size!=width*height:raise ValueError('Reference sample size differs from coded extent')
    results=[]
    for case in report['cases']:
        name=case.get('case','')
        if not re.fullmatch(r'[a-z0-9-]{1,80}',name):raise ValueError('Invalid case name')
        value=dict(case)
        if case.get('result')=='complete' and case.get('last_gop_captured'):
            decoded,count,stream=decode_last(files[name+'-last-gop.hevc'],width,height)
            if count!=case['last_gop_frames']:raise ValueError('Decoded prefix frame count differs from recorded native outputs')
            value.update(last_frame_quality=quality(source,decoded),decoded_prefix_frames=count,stream=stream)
        results.append(value)
    return {'schema':'sfnp-nvenc-quality-analysis-v1','source_revision':report['source_revision'],
            'input_archive_sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest(),'extent':[width,height],
            'cases':results,'scope':'Owned synthetic GPU comparisons only. P-stream bitrate/quality does not establish installed receiver compatibility, loss recovery,90Hz concurrent throughput or actual-game headset quality.','device_actions':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('archive',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    report=analyze(args.archive);args.output.write_text(json.dumps(report,indent=2)+'\n')
    for case in report['cases']:
        print(case['case'],case.get('result'),case.get('steady',{}).get('nominal_bitrate'),case.get('last_frame_quality'))
if __name__=='__main__':main()
