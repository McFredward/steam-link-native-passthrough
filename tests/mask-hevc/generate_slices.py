#!/usr/bin/env python3
"""Generate complete owned non-WPP slices; no hardware/runtime access.

Kvazaar2.3.2's legacy --slices=uN path can produce incomplete pictures.
Use one-column row tiles with one independent slice per tile, then remove
redundant tile signaling and unused temporal-layer declarations. CABAC bytes
stay unchanged. Base decoded YUV planes must equal encoder reconstruction. Filter variants
use an unmodified serial software decoder as their pixel reference.
"""
import argparse, hashlib, json, re, subprocess, tempfile
from pathlib import Path

def normalize(source, ffmpeg, cross_filter=False):
    p = subprocess.run([ffmpeg, '-v', 'debug', '-i', str(source), '-c', 'copy', '-bsf:v', 'trace_headers', '-f', 'null', '-'], capture_output=True, text=True, check=True)
    headers = []
    current = None
    for line in p.stderr.splitlines():
        m = re.search('\\]\\s+(\\d+)\\s+([a-zA-Z0-9_]+(?:\\[\\d+\\])?)\\s+([01]+)\\s+=\\s+(-?\\d+)', line)
        if not m:
            continue
        pos, name, bits, value = m.groups()
        record = (int(pos), int(value), bits)
        if name == 'forbidden_zero_bit':
            current = {}
            headers.append(current)
        if current is not None:
            current[name] = record
    slices = [h for h in headers if h.get('nal_unit_type', (0, -1))[1] in (1, 19, 20)]
    pps = next((h for h in headers if h.get('nal_unit_type', (0, -1))[1] == 34))
    assert pps['num_tile_columns_minus1'][1] == 0 and pps['num_tile_rows_minus1'][1] in (1, 3) and (pps['uniform_spacing_flag'][1] == 1)
    assert pps['entropy_coding_sync_enabled_flag'][1] == pps['loop_filter_across_tiles_enabled_flag'][1] == pps['pps_loop_filter_across_slices_enabled_flag'][1] == 0

    def unescape(ebsp):
        rbsp = bytearray()
        zeros = 0
        for x in ebsp:
            if zeros == 2 and x == 3:
                zeros = 0
                continue
            rbsp.append(x)
            zeros = min(zeros + 1, 2) if x == 0 else 0
        return rbsp

    def escape(raw):
        out = bytearray()
        zeros = 0
        for x in raw:
            if zeros == 2 and x <= 3:
                out.append(3)
                zeros = 0
            out.append(x)
            zeros = min(zeros + 1, 2) if x == 0 else 0
        return out

    def pack(bits):
        bits += '0' * (-len(bits) % 8)
        return bytes((int(bits[j:j + 8], 2) for j in range(0, len(bits), 8)))
    coded = source.read_bytes()
    starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', coded))
    out = bytearray()
    index = 0
    for i, m in enumerate(starts):
        end = starts[i + 1].start() if i + 1 < len(starts) else len(coded)
        body = m.end()
        kind = coded[body] >> 1 & 63
        assert coded[body + 1] == 1
        raw = unescape(coded[body + 2:end])
        bits = ''.join((f'{x:08b}' for x in raw))
        if kind in (32, 33):
            at = 12 if kind == 32 else 4
            after = 128 if kind == 32 else 104
            assert bits[at:at + 4] == '0010' and bits[after:after + 16] == '0' * 16
            bits = bits[:at] + '0001' + bits[at + 4:after] + bits[after + 16:]
            replacement = pack(bits)
        elif kind == 34:
            at = pps['tiles_enabled_flag'][0] - 16
            last = pps['loop_filter_across_tiles_enabled_flag'][0] + 1 - 16
            assert bits[at:at + 2] == '10'
            assert pps['pps_loop_filter_across_slices_enabled_flag'][0] - 16 == last
            tail = ('1' + bits[last + 1:]) if cross_filter else bits[last:]
            replacement = pack((bits[:at] + '00' + tail).rstrip('0'))
        elif kind in (1, 19, 20):
            header = slices[index]
            index += 1
            entry = header['num_entry_point_offsets']
            assert entry[1] == 0 and entry[2] == '1'
            at = entry[0] - 16
            align = header['alignment_bit_equal_to_one'][0] - 16
            assert align == at + 1
            old_offset = (align + 1 + 7) // 8
            replacement = pack(bits[:at] + ('1' if cross_filter else '') + '1') + raw[old_offset:]
        else:
            raise ValueError('Unexpected NAL ' + str(kind))
        out.extend(coded[m.start():body + 2] + escape(replacement))
    assert index == len(slices)
    return bytes(out)

def generate(encoder, source_generator, ffmpeg, output, full=False, slices=2, sao=False, cross_filter=False):
    width, height = (1152, 4608) if full else (256, 256)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sfnp-slice-source-') as directory:
        work = Path(directory)
        raw = work / 'input.yuv'
        coded = work / 'encoded.hevc'
        reconstruction = work / 'reference.yuv'
        with raw.open('wb') as target:
            subprocess.run([str(source_generator), 'full' if full else 'small'], stdout=target, check=True)
        args = [str(encoder), '-i', str(raw), '--input-res', f'{width}x{height}', '--frames', '16', '-o', str(coded), '--debug', str(reconstruction), '--preset', 'ultrafast', '--qp', '40', '--tiles', f'1x{slices}', '--slices', 'tiles', '--no-wpp', '--threads', '1', '--owf', '0', '--gop', '0', '--period', '8', '--no-open-gop', '--ref', '1', '--no-tmvp', '--no-bipred', '--range', 'pc', '--hash', 'none', '--no-info', '--no-aud']
        args += ['--sao', 'full' if sao else 'off']
        subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=120)
        normalized = normalize(coded, ffmpeg)
        flat = work / 'flat.hevc'
        flat.write_bytes(normalized)
        decoded = subprocess.run([str(ffmpeg), '-v', 'error', '-threads', '1', '-thread_type', 'slice', '-i', str(flat), '-pix_fmt', 'yuv420p', '-f', 'rawvideo', '-'], check=True, capture_output=True, timeout=30).stdout
        original = reconstruction.read_bytes()
        plane = width * height
        assert len(original) == len(decoded) == 16 * plane * 3 // 2 and original == decoded, 'Complete encoder reconstruction changed'
        if cross_filter:
            normalized = normalize(coded, ffmpeg, True)
            flat.write_bytes(normalized)
            decoded = subprocess.run([str(ffmpeg), '-v', 'error', '-threads', '1', '-thread_type', 'slice', '-i', str(flat), '-pix_fmt', 'yuv420p', '-f', 'rawvideo', '-'], check=True, capture_output=True, timeout=30).stdout
            assert len(decoded) == len(original)
        reference = [hashlib.sha256(decoded[n * plane * 3 // 2:n * plane * 3 // 2 + plane]).hexdigest() for n in range(16)]
        params = []
        pictures = []
        starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', normalized))
        for i, m in enumerate(starts):
            end = starts[i + 1].start() if i + 1 < len(starts) else len(normalized)
            body = m.end()
            kind = normalized[body] >> 1 & 63
            if kind == 32:
                params = []
            if kind in (32, 33, 34):
                params.append(normalized[m.start():end])
            elif kind in (1, 19, 20):
                if normalized[body + 2] & 128:
                    pictures.append(bytearray(b''.join(params)))
                pictures[-1].extend(normalized[m.start():end])
            else:
                raise ValueError('Unexpected fixture NAL')
        assert len(pictures) == 16
        pins = {}
        for i, au in enumerate(pictures):
            name = f'nal-{i:03d}.hevc'
            data = bytes(au)
            (output / name).write_bytes(data)
            pins[name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        record = {'schema': 'sfnp-owned-nal-slices-v1', 'extent': [width, height], 'frames': 16, 'slices': slices, 'wpp': False, 'tiles': False, 'source': 'owned integer fractional foliage/rain; two IPPP8 GOPs; no image capture', 'encoder_source': 'https://github.com/ultravideo/kvazaar/tree/6040962bed5cc68c5ad01234c38c08b8b2822068', 'encoder_source_archive_sha256': '12fd0f8f63627c8ce7c8e8398c0dddbaa9e3c7bec9d9af9260f45447c4d56fae', 'encoder_options': [x if not str(work) in x else Path(x).name for x in args[1:]], 'header_normalization': 'one unused temporal sublayer removed; row tiles coincide with independent slices; remove tiles and zero entry-point field, retain entropy' + ('; enable cross-slice loop filtering in PPS and slice headers, reference from unmodified serial decoder' if cross_filter else '; retain disabled cross-slice filters'), 'base_normalization_encoder_reconstruction_equal': True, 'encoder_reconstruction_all_YUV_bytes_equal': not cross_filter, 'reference_kind': 'unmodified-serial-decoder' if cross_filter else 'encoder-reconstruction', 'sao': sao, 'cross_slice_filter': cross_filter, 'reference_Y_sha256': reference, 'files': pins, 'device_actions': False}
        (output / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
        return record

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('encoder', 'source-generator', 'ffmpeg', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--sao', action='store_true')
    parser.add_argument('--cross-filter', action='store_true')
    parser.add_argument('--full', action='store_true')
    parser.add_argument('--slices', type=int, choices=(2, 4), default=2)
    args = parser.parse_args()
    record = generate(args.encoder.resolve(), args.source_generator.resolve(), args.ffmpeg.resolve(), args.output.resolve(), args.full, args.slices, args.sao, args.cross_filter)
    print(json.dumps({'result': 'PASS', 'extent': record['extent'], 'frames': 16, 'slices': record['slices'], 'base_all_YUV_bytes_match_encoder': True, 'reference_kind': record['reference_kind'], 'device_actions': False}))
if __name__ == '__main__':
    main()
