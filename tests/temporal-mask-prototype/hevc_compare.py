#!/usr/bin/env python3
"""Private offline HEVC mask study; no Steam/Frame changes or production policy.

Build helper (from repository root, clean dev baseline):
 cmake -S . -B build/temporal -G Ninja -DFRAME_STREAM_BRIDGE_ONLY=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
 cmake --build build/temporal --target sfnp_stream_bridge --parallel 4
 g++ -std=c++17 -O3 -I. tests/temporal-mask-prototype/{prototype,main}.cpp build/temporal/libsfnp_stream_bridge.a -o build/temporal-prototype

Example: --helper build/temporal-prototype --output build/hevc-study --sequences 1,2,3
Optional --capture latest.sfmc --capture-reader tools/capture_masks.py validates
all source/encoder/extent/frame continuity first; it never stitches captures.
An explicit frame limit is recorded. --fps overrides nominal timing only.
Lossy qualities are OPTIONAL measurements, never installation or release flags.
"""
import argparse
import csv
from fractions import Fraction
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import time

import numpy as np

NAMES=['static-cloud-control','moving-animated-clouds','moving-particles','gaze-like-remapping-stress','abrupt-scenes','full-noise-stress','near-opaque-noise-stress']
MAX_RAW=256*1024**2


def run(args, **kwargs):
    result=subprocess.run(list(map(str,args)), check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
    if result.returncode:
        raise RuntimeError('Command failed: '+str(args)+'\n'+result.stderr.decode(errors='replace')[-4000:])
    return result


def percentile(values, q=.95):
    values=sorted(values)
    return values[min(len(values)-1,int(len(values)*q))] if values else 0


def capture_input(args):
    reader=args.capture_reader or Path(__file__).resolve().parents[2]/'tools/capture_masks.py'
    spec=importlib.util.spec_from_file_location('sfnp_private_capture',reader)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    records=module.capture_records(args.capture);rows=[]
    while True:
        try:rows.append(next(records))
        except StopIteration as finished:
            summary=finished.value;break
    if not rows:raise RuntimeError('Capture contains no complete frames')
    first=rows[0][0];w,h=first['width'],first['height'];size=w*h
    selected=rows[:args.frames]
    if size*len(selected)>MAX_RAW:raise RuntimeError('Selected raw masks exceed256MiB; explicitly lower --frames')
    raw=bytearray()
    for meta,encoded in selected:
        frame=run([args.helper,'--decode-mask',meta['encoding'],w,h],input=encoded).stdout
        if len(frame)!=size:raise RuntimeError('Native capture decoder produced a different extent')
        raw.extend(frame)
    rates={Fraction(m['fps_num'],m['fps_den']) for m,_ in selected if m['fps_num'] and m['fps_den']}
    if args.fps is None and (len(rates)!=1 or any(not m['fps_num'] or not m['fps_den'] for m,_ in selected)):
        raise RuntimeError('Capture lacks a single observed nominal encoder fps; specify --fps explicitly')
    fps=Fraction(args.fps) if args.fps else rates.pop()
    context={'capture_sha256':hashlib.sha256(args.capture.read_bytes()).hexdigest(),'capture_reader_sha256':hashlib.sha256(reader.read_bytes()).hexdigest(),
             'capture_summary':summary,'frames_available':len(rows),'frames_tested':len(selected),'source_frame_ids':[m['frame'] for m,_ in selected],
             'encoder_epoch':first['encoder_generation'],'source_epoch':first['source_contract'],'width':w,'height':h,
             'fps_origin':'explicit-cli' if args.fps else 'recorded-constant-nominal-fps','fps_num':fps.numerator,'fps_den':fps.denominator}
    return [('capture',raw,w,h,fps,context)]


def inputs(args):
    if args.capture:return capture_input(args)
    datasets=[]
    # Keep only one raw sequence live: generate on demand in the main loop.
    for code in map(int,args.sequences.split(',')):
        if not 0<=code<len(NAMES):raise RuntimeError('Unknown synthetic sequence')
        fps=Fraction(args.fps or '90')
        datasets.append((NAMES[code],code,1152,4608,fps,{'synthetic':True,'frames_tested':args.frames,'fps_origin':'synthetic nominal'}))
    return datasets


def frame_input(raw,w,h,pixel):
    if pixel=='gray':return raw
    if w%2 or h%2:raise RuntimeError('420 input needs even dimensions; the tool never resizes masks')
    count=len(raw)//(w*h);plane=w*h;result=bytearray(count*plane*3//2);neutral=bytes([128])*(plane//2)
    for i in range(count):
        offset=i*plane*3//2;result[offset:offset+plane]=raw[i*plane:(i+1)*plane]
        result[offset+plane:offset+plane*3//2]=neutral
    return result


def bench_times(log):
    found=re.search(r'bench: utime=([0-9.]+)s stime=([0-9.]+)s rtime=([0-9.]+)s',log)
    return dict(zip(('user_seconds','system_seconds','wall_seconds'),map(float,found.groups()))) if found else {}


def csv_stats(path):
    rows=[]
    if path.exists():
        with path.open() as stream:
            for row in csv.DictReader(stream,skipinitialspace=True):
                row={str(k).strip():v.strip() if v else '' for k,v in row.items() if k is not None}
                if row.get('Type') in ('I-SLICE','P-SLICE','B-SLICE'):rows.append(row)
    out={}
    for key in ('Wall time (ms)','Total frame time (ms)','DecideWait (ms)','Latency'):
        values=[float(r[key]) for r in rows if r.get(key)]
        if values:out[key]={'mean':sum(values)/len(values),'p95':percentile(values),'max':max(values)}
    out['frame_types']={t:sum(r.get('Type')==t for r in rows) for t in ('I-SLICE','P-SLICE','B-SLICE')}
    return out


def quality_check(args,path,raw,w,h,pixel,stream,expected_indices=None):
    # Preserve the decoder's actual full-range format. Forcing yuvj420p ->
    # limited yuv420p would itself alter alpha values and invalidate this study.
    fmt=stream['pix_fmt'];plane=w*h;frame_bytes=plane if pixel=='gray' else plane*3//2
    command=[args.ffmpeg,'-hide_banner','-nostdin','-v','error','-i',str(path),'-fps_mode','passthrough','-pix_fmt',fmt,'-f','rawvideo','pipe:1']
    proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    hist=np.zeros(256,dtype=np.uint64);fractional_hist=hist.copy();variation=np.zeros(511,dtype=np.uint64)
    endpoints={'raw0_samples':0,'raw255_samples':0,'raw0_to_nonzero':0,'raw255_to_non255':0,'raw0_max_error':0,'raw255_max_error':0}
    mismatching=[];previous_error=None;seen=0;exact=0;fractional=0
    indices=expected_indices or list(range(len(raw)//plane))
    try:
        while True:
            data=proc.stdout.read(frame_bytes)
            if not data:break
            if len(data)!=frame_bytes or seen>=len(indices):raise RuntimeError('Decoded frame count/extent mismatch')
            index=indices[seen];original=np.frombuffer(raw,dtype=np.uint8,count=plane,offset=index*plane);actual=np.frombuffer(data,dtype=np.uint8,count=plane)
            signed=actual.astype(np.int16)-original.astype(np.int16);absolute=np.abs(signed)
            counts=np.bincount(absolute,minlength=256).astype(np.uint64);hist+=counts
            if counts[0]==plane:exact+=1
            else:mismatching.append(index)
            fraction=(original>0)&(original<255);fractional+=int(fraction.sum());fractional_hist+=np.bincount(absolute[fraction],minlength=256).astype(np.uint64)
            zero=original==0;opaque=original==255
            endpoints['raw0_samples']+=int(zero.sum());endpoints['raw255_samples']+=int(opaque.sum())
            endpoints['raw0_to_nonzero']+=int(np.count_nonzero(zero&(actual!=0)));endpoints['raw255_to_non255']+=int(np.count_nonzero(opaque&(actual!=255)))
            if zero.any():endpoints['raw0_max_error']=max(endpoints['raw0_max_error'],int(absolute[zero].max()))
            if opaque.any():endpoints['raw255_max_error']=max(endpoints['raw255_max_error'],int(absolute[opaque].max()))
            if previous_error is not None:variation+=np.bincount(np.abs(signed-previous_error),minlength=511).astype(np.uint64)
            previous_error=signed;seen+=1
        errors=proc.stderr.read().decode(errors='replace');status=proc.wait(timeout=20)
        if status:raise RuntimeError('HEVC decoder failed: '+errors[-3000:])
    finally:
        if proc.poll() is None:proc.kill();proc.wait()
    if seen!=len(indices):raise RuntimeError('HEVC decoder omitted frames without an explicit loss map')
    def metrics(h):
        n=int(h.sum());cumulative=h.cumsum();values=np.arange(len(h),dtype=np.uint64)
        return {'samples':n,'mae':float((h*values).sum())/n if n else 0,'p95':int(np.searchsorted(cumulative,math.ceil(n*.95))) if n else 0,
                'p99':int(np.searchsorted(cumulative,math.ceil(n*.99))) if n else 0,'max':int(np.flatnonzero(h)[-1]) if n else 0,'changed':n-int(h[0])}
    return {'frames':seen,'exact_frames':exact,'mismatching_frame_indices':mismatching,'all':metrics(hist),'fractional':metrics(fractional_hist),
            'temporal_error_change':metrics(variation),'endpoints':endpoints,'decoder_warnings':errors[-3000:]}


def probe(args,path,packets=False):
    entries='packet=pts_time,size,flags,data_hash' if packets else 'stream=codec_name,profile,level,pix_fmt,width,height,has_b_frames,color_range'
    result=run([args.ffprobe,'-v','error','-select_streams','v:0','-show_entries',entries,'-show_data_hash','sha256','-of','json',path])
    return json.loads(result.stdout)['packets' if packets else 'streams']


def encode_case(args,root,name,raw,w,h,fps,pixel,quality,gop):
    tag=f'{name}-{pixel}-{quality}-gop{gop}';path=root/(tag+'.mkv');csv_path=root/(tag+'.csv');frames=len(raw)//(w*h)
    # x265 appends its CSV even when FFmpeg overwrites the coded file. Keep a
    # repeated measurement from silently mixing samples from earlier runs.
    csv_path.unlink(missing_ok=True)
    # keyint=1 makes x265 select the RExt Main Intra profile. For the Main
    # hardware-oriented control, keep a legal Main GOP configuration and force
    # every actual picture to IDR; verify the resulting profile and packets.
    keyint=max(2,gop)
    params=f'bframes=0:rc-lookahead=0:frame-threads=1:pools={args.threads}:wpp=1:keyint={keyint}:min-keyint={keyint}:scenecut=0:open-gop=0:ref=1:aud=1:repeat-headers=1:log-level=error:csv={csv_path}:csv-log-level=2'
    quality_args=[]
    if quality=='lossless':params+=':lossless=1'
    elif quality.startswith('crf'):quality_args=['-crf',quality[3:]]
    elif quality.startswith('abr'):
        kbps=int(quality[3:])*1000;buffer=max(1,round(kbps/float(fps)));params+=f':bitrate={kbps}:vbv-maxrate={kbps}:vbv-bufsize={buffer}:vbv-init=1'
    else:raise RuntimeError('Unknown quality: '+quality)
    payload=frame_input(raw,w,h,pixel)
    intra_args=['-force_key_frames','expr:gte(n,n_forced)','-forced-idr','1'] if gop==1 else []
    command=[args.ffmpeg,'-hide_banner','-nostdin','-benchmark','-f','rawvideo','-pixel_format','gray' if pixel=='gray' else 'yuvj420p','-color_range','pc','-video_size',f'{w}x{h}',
             '-framerate',str(fps),'-i','pipe:0','-frames:v',str(frames),'-c:v','libx265','-preset',args.preset,'-tune','zerolatency',*quality_args,
             *intra_args,'-x265-params',params,'-color_range','pc','-y',str(path)]
    result=run(command,input=payload);del payload
    log=result.stderr.decode(errors='replace');(root/(tag+'.encode.log')).write_text(log)
    stream=probe(args,path)[0]
    if stream['width']!=w or stream['height']!=h or stream['has_b_frames']!=0:raise RuntimeError('Encoder changed extent or introduced B-frames')
    packets=probe(args,path,True);sizes=[int(p['size']) for p in packets];keys=[int(p['size']) for p in packets if 'K' in p.get('flags','')]
    if gop==1 and len(keys)!=frames:raise RuntimeError('All-IDR control produced a non-key picture')
    checked=quality_check(args,path,raw,w,h,pixel,stream)
    if quality=='lossless' and checked['exact_frames']!=frames:raise RuntimeError('Lossless HEVC changed R8 samples')
    decoder=run([args.ffmpeg,'-hide_banner','-nostdin','-benchmark','-i',path,'-fps_mode','passthrough','-f','null','-'])
    decoder_log=decoder.stderr.decode(errors='replace');(root/(tag+'.decode.log')).write_text(decoder_log)
    times=bench_times(log);decode_times=bench_times(decoder_log)
    record={'sequence':name,'format':pixel,'quality':quality,'lossless':quality=='lossless','frames':frames,'width':w,'height':h,'fps_num':fps.numerator,'fps_den':fps.denominator,
            'gop':gop,'preset':args.preset,'pool_threads':args.threads,'stream':stream,'hevc_packet_bytes':sum(sizes),'mean_au_bytes':sum(sizes)/len(sizes),'max_au_bytes':max(sizes),
            'keyframes':len(keys),'max_keyframe_bytes':max(keys),'nominal_mask_mbps':sum(sizes)*8*float(fps)/frames/1e6,
            'encoder_benchmark':times,'encoder_csv':csv_stats(csv_path),'encoder_throughput_fps':frames/times['wall_seconds'] if times.get('wall_seconds') else None,
            'decoder_benchmark':decode_times,'decoder_null_throughput_fps':frames/decode_times['wall_seconds'] if decode_times.get('wall_seconds') else None,
            'quality_metrics':checked,'path':str(path),'ffmpeg_command':command}
    return record,path,packets


def dropped_frame_test(args,root,path,packets,raw,w,h,fps,pixel,stream,index):
    target=root/(path.stem+f'-drop{index}.mkv')
    run([args.ffmpeg,'-hide_banner','-nostdin','-v','error','-i',path,'-map','0:v:0','-c:v','copy','-bsf:v',f"noise=drop='eq(n,{index})'",'-y',target])
    retained=probe(args,target,True);original={p['pts_time']:p['data_hash'] for p in packets}
    if any(original.get(p['pts_time'])!=p['data_hash'] for p in retained):raise RuntimeError('Drop control altered a retained coded packet')
    frames=run([args.ffprobe,'-v','error','-select_streams','v:0','-show_entries','frame=pts_time','-of','json',target])
    frame_indices=[round(float(f['pts_time'])*float(fps)) for f in json.loads(frames.stdout)['frames'] if 'pts_time' in f]
    checked=quality_check(args,target,raw,w,h,pixel,stream,frame_indices)
    return {'drop_packet_index':index,'retained_packets':len(retained),'unchanged_retained_packet_hashes':True,'decoded_source_frame_indices':frame_indices,'quality_metrics':checked}


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--helper',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sequences',default='1,2,3');parser.add_argument('--frames',type=int,default=24);parser.add_argument('--fps')
    parser.add_argument('--formats',default='420');parser.add_argument('--qualities',default='lossless,crf18,crf28,abr50');parser.add_argument('--gop',type=int,default=15)
    parser.add_argument('--preset',default='ultrafast');parser.add_argument('--threads',type=int,default=4)
    parser.add_argument('--capture',type=Path);parser.add_argument('--capture-reader',type=Path)
    parser.add_argument('--loss-tests',action='store_true');parser.add_argument('--keep-streams',action='store_true')
    parser.add_argument('--ffmpeg',default='ffmpeg');parser.add_argument('--ffprobe',default='ffprobe')
    args=parser.parse_args();args.helper=args.helper.resolve();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    if not 1<=args.frames<=128 or not 1<=args.threads<=16 or not 1<=args.gop<=120:parser.error('Bounded frames/threads/GOP required')
    if args.output==Path('/'):parser.error('Choose a private output directory')
    version=run([args.ffmpeg,'-version']).stdout.decode(errors='replace').splitlines()[:3]
    all_records=[];contexts=[]
    for name,raw,w,h,fps,context in inputs(args):
        if isinstance(raw,int):raw=run([args.helper,'--raw',raw,args.frames,'gray']).stdout
        if len(raw)>MAX_RAW or len(raw)%(w*h):raise RuntimeError('Raw sequence exceeds memory cap or extent')
        baseline_rows=[json.loads(line) for line in run([args.helper,'--baseline-r8',w,h],input=raw).stdout.decode().splitlines()]
        context.update(sequence=name,current_codec_payload_bytes=sum(r['bytes'] for r in baseline_rows),
                       current_codec_mean_mask_bytes=sum(r['bytes'] for r in baseline_rows)/len(baseline_rows),
                       current_codec_nominal_mask_mbps=sum(r['bytes'] for r in baseline_rows)*8*float(fps)/len(baseline_rows)/1e6,
                       current_codec_encode_p95_ms=percentile([r['encode_ms'] for r in baseline_rows]),
                       current_codec_decode_compare_p95_ms=percentile([r['decode_ms'] for r in baseline_rows]),baseline_frames=baseline_rows)
        contexts.append(context)
        for pixel in args.formats.split(','):
            if pixel not in ('gray','420'):raise RuntimeError('Only gray or420 planes are supported')
            for quality in args.qualities.split(','):
                record,path,packets=encode_case(args,args.output,name,raw,w,h,fps,pixel,quality,args.gop)
                if args.loss_tests and quality=='lossless' and len(packets)>args.gop:
                    record['loss_controls']=[dropped_frame_test(args,args.output,path,packets,raw,w,h,fps,pixel,record['stream'],i) for i in (7,args.gop)]
                all_records.append(record)
                print(json.dumps({k:record[k] for k in ('sequence','format','quality','nominal_mask_mbps','encoder_throughput_fps','stream')}),flush=True)
                if not args.keep_streams:
                    path.unlink()
                    for dropped in args.output.glob(path.stem+'-drop*.mkv'):dropped.unlink()
        del raw
        (args.output/'results.json').write_text(json.dumps({'kind':'private-established-hevc-mask-study','ffmpeg':version,'actual_game_hardware_test':False,
            'lossy_is_optional_research_only':True,'source_contexts':contexts,'results':all_records},indent=2)+'\n')


if __name__=='__main__':main()
