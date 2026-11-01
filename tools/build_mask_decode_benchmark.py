#!/usr/bin/env python3
"""Build offline decoder variants; never installs or controls a device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Decoder instrumentation no longer matches this source')
    return text.replace(before, after)


def instrument(source, grayscale=False):
    color = 'AV_CODEC_FLAG_COPY_OPAQUE|AV_CODEC_FLAG_LOW_DELAY;'
    gray = 'AV_CODEC_FLAG_COPY_OPAQUE|AV_CODEC_FLAG_LOW_DELAY|AV_CODEC_FLAG_GRAY;'
    if source.count(color) + source.count(gray) != 1:
        raise ValueError('Decoder grayscale selection no longer matches this source')
    source = source.replace(gray if gray in source else color, gray if grayscale else color)
    source = replace_once(source, 'namespace stream_bridge_mask_hevc {',
        'extern void sfnpBenchmarkStage(unsigned, std::chrono::steady_clock::time_point) noexcept;\n'
        'namespace stream_bridge_mask_hevc {')
    source = replace_once(source, 'if(avcodec_send_packet(w.context,w.packet)<0)return Status::DecoderRefused;',
        'const auto benchSend=std::chrono::steady_clock::now();\n'
        '        const int benchSent=avcodec_send_packet(w.context,w.packet);\n'
        '        sfnpBenchmarkStage(0,benchSend);if(benchSent<0)return Status::DecoderRefused;')
    source = replace_once(source, 'int received=avcodec_receive_frame(w.context,w.frame);',
        'const auto benchReceive=std::chrono::steady_clock::now();\n'
        '        int received=avcodec_receive_frame(w.context,w.frame);sfnpBenchmarkStage(1,benchReceive);')
    source = replace_once(source, 'for(unsigned y=0;y<view.height;++y){',
        'const auto benchCopy=std::chrono::steady_clock::now();\n'
        '        for(unsigned y=0;y<view.height;++y){')
    source = replace_once(source, 'av_frame_unref(w.frame);received=avcodec_receive_frame(w.context,w.frame);',
        'sfnpBenchmarkStage(2,benchCopy);\n'
        '        av_frame_unref(w.frame);received=avcodec_receive_frame(w.context,w.frame);')
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--include', type=Path, required=True, help='Configured public FFmpeg8 SDK headers')
    parser.add_argument('--output', type=Path, required=True, help='Fresh owned build directory')
    parser.add_argument('--compiler', default='aarch64-linux-gnu-g++')
    parser.add_argument('--grayscale', action='store_true', help='Benchmark the public grayscale decode flag')
    parser.add_argument('--threads', type=int, nargs='+', choices=(1, 2, 4), default=(1, 2, 4))
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a fresh output directory; existing files are preserved')
    args.output.mkdir(parents=True)
    original = (ROOT / 'receiver/stream_bridge_mask_hevc.cpp').read_text()
    header = (ROOT / 'receiver/stream_bridge_mask_hevc.h').read_text()
    instrumented = instrument(original, args.grayscale)
    records = []
    for threads in dict.fromkeys(args.threads):
        folder = args.output / str(threads)
        receiver = folder / 'receiver'
        receiver.mkdir(parents=True)
        decoder = receiver / 'stream_bridge_mask_hevc.cpp'
        decoder.write_text(instrumented)
        (receiver / 'stream_bridge_mask_hevc.h').write_text(replace_once(header,
            'SliceThreads=2;', f'SliceThreads={threads};'))
        binary = folder / 'mask-decode-benchmark'
        command = [args.compiler, '-std=c++17', '-O3', '-DNDEBUG', '-pthread', '-Wall', '-Wextra', '-Werror',
            '-DSFNP_MASK_HEVC_ENABLED=1', '-I' + str(folder.resolve()), '-I' + str(ROOT),
            '-I' + str(args.include.resolve()), str(ROOT / 'probe/mask_decode_benchmark.cpp'),
            str(decoder), str(ROOT / 'bridge/stream_bridge_hevc_coverage.cpp'), '-ldl', '-o', str(binary)]
        subprocess.run(command, check=True, timeout=90)
        records.append({'slice_threads': threads, 'binary': str(binary.resolve()),
                        'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()})
    manifest = {'schema': 'sfnp-mask-benchmark-build-v1',
                'compiler': subprocess.check_output([args.compiler, '-dumpfullversion'], text=True).strip(),
                'production_decoder_sha256': hashlib.sha256(original.encode()).hexdigest(),
                'production_header_sha256': hashlib.sha256(header.encode()).hexdigest(),
                'public_grayscale_flag': args.grayscale,
                'instrumentation': 'private source copies only; send, receive and R8 copy/reduction clocks',
                'installed_components_modified': False, 'variants': records}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest))


if __name__ == '__main__':
    main()
