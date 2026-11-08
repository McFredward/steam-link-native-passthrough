#!/usr/bin/env python3
"""Locally derive a full-colour native fragment with a separate coverage sampler.

Only the exact installed native module is accepted. Native RGB reconstruction,
reprojection UV and every original instruction are retained. Generated native
code is private installation output and must never be distributed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import native_shader_contract as shader_contract
import native_fragment_ssa_probe as ssa
import native_fragment_count1_interface as count1


def transform(original, descriptor_count=1, *, camera_only=False):
    original = shader_contract.normalize(original, 'fragment')
    ssa.require(type(descriptor_count) is int and descriptor_count in (1, 3),
                'explicit owned descriptor count1 or3 required')
    if hashlib.sha256(original).hexdigest() == shader_contract._METADATA['fragment_beta']['sha256']:
        return transform_current(original, descriptor_count, camera_only=camera_only)
    ssa.require(len(original) == ssa.ORIGINAL_BYTES and
                hashlib.sha256(original).hexdigest() == ssa.ORIGINAL_SHA256,
                "unsupported native fragment")
    # Reuse the established original-only constant-zero B9 proof before adding
    # our explicit-LOD mask reads. No native layout or executed access changes.
    owned_original = (count1.specialize_owned_binding9_count1(original, original)
                      if descriptor_count == 1 else original)
    native_header, instructions = ssa.parse_module(owned_original)
    header = list(native_header)
    _, store_index = ssa.profile_structure(header, instructions)
    ssa.require(instructions[store_index].operands == (504, 994), "native output changed")
    # IDs 538 (the actual native coded-video sample UV), 899 (native aperture),
    # and 994 (unchanged original RGBA) dominate this exact output store.
    ids = iter(range(header[3], header[3] + 22))
    pointer, variable, loaded, sample, second, value, inner, outer_weight, a, b, total, alpha, output, control_struct, control_pointer, control_variable, float_pointer, valid_address, valid, inverse_valid, ordinary_alpha, chosen_alpha = list(ids)
    make = lambda op, *args: ssa.Instruction(op, tuple(args))
    decorations = [make(71, variable, 34, 1), make(71, variable, 33, 0),
                   make(71, control_struct, 2), make(72, control_struct, 0, 35, 0),
                   make(71, control_variable, 34, 2), make(71, control_variable, 33, 0)]
    declarations = [make(32, pointer, 0, 297), make(59, pointer, variable, 0),
                    make(30, control_struct, 8),
                    make(32, control_pointer, 2, control_struct),
                    make(32, float_pointer, 2, 8),
                    make(59, control_pointer, control_variable, 2)]
    # Explicit LOD zero, same original full-video UV. No atlas/crop/resize,
    # copied eye matrix, eye index inference or RGB multiplication by alpha.
    composition = [make(61, 297, loaded, variable),
                   make(88, 9, sample, loaded, 538, 2, 210),
                   make(88, 9, second, loaded, 798, 2, 210),
                   make(81, 8, value, sample, 0),
                   make(81, 8, inner, second, 0),
                   make(131, 8, outer_weight, 185, 1008),
                   make(133, 8, a, value, outer_weight),
                   make(133, 8, b, inner, 1008),
                   make(129, 8, total, a, b),
                   make(65, float_pointer, valid_address, control_variable, 95),
                   make(61, 8, valid, valid_address),
                   make(131, 8, inverse_valid, 185, valid),
                   make(133, 8, alpha, total, 899),
                   make(133, 8, ordinary_alpha, alpha, valid),
                   make(129, 8, chosen_alpha, ordinary_alpha, inverse_valid),
                   make(82, 9, output, chosen_alpha, 994, 3)]
    # A second owned control value is an active-source camera-only recovery
    # grant. It never authorizes game samples. An invalid exact guard plus this
    # grant emits constant zero RGBA, while ordinary/exact modes retain their
    # previous native RGB/alpha outputs bit-for-bit.
    bool_type = next(inst.operands[0] for inst in instructions if inst.opcode == 20)
    zero_type = next(inst.operands[0] for inst in instructions if inst.opcode == 43 and inst.operands[1] == 95)
    one_index, zero_rgba, fallback_address, fallback, wants_camera, invalid, recover, final, bool_vector, condition = range(header[3] + 22, header[3] + 32)
    decorations.append(make(72, control_struct, 1, 35, 4))
    declarations = [make(30, control_struct, 8, 8) if inst.opcode == 30 and inst.operands[0] == control_struct else inst for inst in declarations]
    declarations.extend([make(23, bool_vector, bool_type, 4), make(43, zero_type, one_index, 1), make(44, 9, zero_rgba, 210, 210, 210, 210)])
    recovery = [make(65, float_pointer, fallback_address, control_variable, one_index),
                make(61, 8, fallback, fallback_address), make(180, bool_type, wants_camera, fallback, 185)]
    if camera_only:
        composition = recovery + [make(82, 9, output, 185, 994, 3),
                                  make(80, bool_vector, condition, wants_camera, wants_camera, wants_camera, wants_camera),
                                  make(169, 9, final, condition, zero_rgba, output)]
    else:
        composition.extend(recovery + [make(180, bool_type, invalid, valid, 210),
                                        make(167, bool_type, recover, wants_camera, invalid),
                                        make(80, bool_vector, condition, recover, recover, recover, recover),
                                        make(169, 9, final, condition, zero_rgba, output)])
    result = []
    annotations = declarations_added = False
    for index, instruction in enumerate(instructions):
        if not annotations and instruction.opcode in ssa.TYPE_OPS:
            result.extend(decorations)
            annotations = True
        if not declarations_added and instruction.opcode == 54:
            result.extend(declarations)
            declarations_added = True
        if index == store_index:
            result.extend(composition)
            instruction = make(62, 504, final)
        result.append(instruction)
    ssa.require(annotations and declarations_added, "native declaration framing changed")
    header[3] = condition + 1  # first unallocated ID
    words = [*header, *(word for instruction in result for word in instruction.words())]
    binary = struct.pack('<%dI' % len(words), *words)
    return binary, {
        'schema': 'sfnp-native-stream-coverage-v1',
        'original_sha256': hashlib.sha256(original).hexdigest(),
        'output_sha256': hashlib.sha256(binary).hexdigest(),
        'output_bytes': len(binary), 'mask_set': 1, 'mask_binding': 0,
        'owned_binding9_descriptor_count': descriptor_count,
        'mask_type': 'combined-image-sampler R8_UNORM',
        'recording_control_set': 2, 'recording_control_binding': 0,
        'recording_control': 'owned coherent floats: exact 0/1; camera-only recovery 0/1. Recovery hides every game pixel when exact is absent',
        'camera_only_shader': camera_only,
        'mask_uv': 'unchanged current native coded-video UVs 538/798 and reconstruction weight 1008',
        'native_rgb_instructions_retained': True,
        'source_and_selected_image_authority_provided_by_runtime': False,
    }


def transform_current(original, descriptor_count=1, *, camera_only=False):
    """Mirror the newer native sample branches and its consumed blend weight.

    Numeric roles identify the reviewed logic; native instructions come only
    from the local input. Coverage follows each original branch, including the
    zero outside that sample's valid region. It never guesses an eye or UV.
    """
    profile = shader_contract._METADATA['fragment_beta']
    ssa.require(hashlib.sha256(original).hexdigest() == profile['sha256'],
                'reviewed current native fragment required')
    words = struct.unpack('<%dI' % (len(original) // 4), original)
    header, native, at = list(words[:5]), [], 5
    while at < len(words):
        n = words[at] >> 16
        ssa.require(n and at + n <= len(words), 'complete native instructions required')
        native.append(ssa.Instruction(words[at] & 65535, tuple(words[at+1:at+n])))
        at += n
    make = lambda op, *a: ssa.Instruction(op, tuple(a))
    fresh = iter(range(header[3], header[3]+128))
    allocate = lambda: next(fresh)
    # Only our replacement interface changes array extent. The native array
    # has exactly two accesses, both constant descriptor0.
    accesses = [i for i in native if i.opcode == 65 and i.operands[2] == 220]
    constants = {i.operands[1]:i.operands[2] for i in native if i.opcode == 43}
    ssa.require(len(accesses) == 2 and all(constants.get(i.operands[3]) == 0 for i in accesses),
                'native video descriptor accesses must all be constant zero')
    one_index, array, array_pointer = [allocate() for _ in range(3)]
    pointer, variable, control, control_pointer, control_variable, float_pointer = [allocate() for _ in range(6)]
    zero_rgba, bool_vector = allocate(), allocate()
    decorations = [make(71, variable, 34, 1), make(71, variable, 33, 0),
                   make(71, control, 2), make(72, control, 0, 35, 0), make(72, control, 1, 35, 4),
                   make(71, control_variable, 34, 2), make(71, control_variable, 33, 0)]
    declarations = [make(32, pointer, 0, 229), make(59, pointer, variable, 0),
                    make(30, control, 6, 6), make(32, control_pointer, 2, control),
                    make(32, float_pointer, 2, 6), make(59, control_pointer, control_variable, 2),
                    make(44, 27, zero_rgba, 94, 94, 94, 94), make(23, bool_vector, 128, 4)]
    before, after = {}, {}
    masks = {}
    for source, uv in ((447,446),(459,458)):
        loaded, sample, value = allocate(), allocate(), allocate()
        masks[source] = value
        after[source] = [make(61,229,loaded,variable),make(88,27,sample,loaded,uv,2,94),make(81,6,value,sample,0)]
    phis = []
    for native_phi, native_value, source in ((660,448,447),(661,460,459)):
        phi = next(i for i in native if i.opcode == 245 and i.operands[1] == native_phi)
        pairs = list(phi.operands[2:])
        ssa.require(len(pairs) == 4 and set(pairs[::2]) == {210,native_value}, 'native sample merge changed')
        for n in range(0,len(pairs),2):
            pairs[n] = masks[source] if pairs[n] == native_value else 94
        value = allocate();phis.append(value)
        before[native_phi] = [make(245,6,value,*pairs)]
    inverse, a, b, total, apertured, valid_address, valid, inverse_valid, alpha, chosen = [allocate() for _ in range(10)]
    fallback_address, fallback, wants_camera, invalid, recover, output, final, condition = [allocate() for _ in range(8)]
    recovery = [make(65,float_pointer,fallback_address,control_variable,one_index),
                make(61,6,fallback,fallback_address),make(180,128,wants_camera,fallback,68)]
    composition = [make(131,6,inverse,68,665),make(133,6,a,phis[0],inverse),
                   make(133,6,b,phis[1],665),make(129,6,total,a,b),make(133,6,apertured,total,511),
                   make(65,float_pointer,valid_address,control_variable,221),make(61,6,valid,valid_address),
                   make(131,6,inverse_valid,68,valid),make(133,6,alpha,apertured,valid),
                   make(129,6,chosen,alpha,inverse_valid),make(82,27,output,chosen,650,3)]
    if camera_only:
        composition = recovery + [make(82,27,output,68,650,3),
                                  make(80,bool_vector,condition,wants_camera,wants_camera,wants_camera,wants_camera),
                                  make(169,27,final,condition,zero_rgba,output)]
    else:
        composition += recovery + [make(180,128,invalid,valid,94),make(167,128,recover,wants_camera,invalid),
                                   make(80,bool_vector,condition,recover,recover,recover,recover),
                                   make(169,27,final,condition,zero_rgba,output)]
    result=[];annotations=globals_added=False
    # This declaration also supplies the UBO member1 index in camera-only mode.
    if descriptor_count == 3:
        declarations.insert(0,make(43,154,one_index,1))
    for item in native:
        if not annotations and item.opcode in ssa.TYPE_OPS:
            result.extend(decorations);annotations=True
        if not globals_added and item.opcode == 54:
            result.extend(declarations);globals_added=True
        if item.opcode == 59 and item.operands[1] == 220 and descriptor_count == 1:
            result.extend([make(43,154,one_index,1),make(28,array,216,one_index),
                           make(32,array_pointer,0,array),make(59,array_pointer,220,0)])
        else:
            ident = item.operands[1] if item.opcode in (88,245) else None
            if not camera_only:result.extend(before.get(ident,[]))
            if item.opcode == 62 and item.operands == (337,650):
                result.extend(composition);item=make(62,337,final)
            result.append(item)
            if not camera_only:result.extend(after.get(ident,[]))
    header[3]=next(fresh)
    binary=struct.pack('<%dI' % (5+sum(len(i.operands)+1 for i in result)),
                       *header,*(word for i in result for word in i.words()))
    return binary, {'schema':'sfnp-native-stream-coverage-v1','original_sha256':profile['sha256'],
                    'output_sha256':hashlib.sha256(binary).hexdigest(),'output_bytes':len(binary),
                    'owned_binding9_descriptor_count':descriptor_count,'camera_only_shader':camera_only,
                    'native_rgb_instructions_retained':True,'native_sample_branches_retained':True,
                    'mask_uv':'current native sample UVs and consumed blend weight',
                    'source_and_selected_image_authority_provided_by_runtime':False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'output', 'record'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        ssa.require(len({p.absolute() for p in (args.original, args.output, args.record)}) == 3,
                    'distinct paths required')
        ssa.require(not any(p.exists() or p.is_symlink() for p in (args.output, args.record)),
                    'outputs must not exist')
        binary, record = transform(ssa.recovery.read_owned(args.original, ssa.MAX_BYTES))
        with tempfile.TemporaryDirectory(prefix='sfnp-private-coverage-') as directory:
            candidate = Path(directory) / 'candidate.spv'
            candidate.write_bytes(binary)
            subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(candidate)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        record['spirv_val_passed'] = True
        ssa.recovery.write_owned(args.output, binary)
        ssa.recovery.write_owned(args.record, (json.dumps(record, indent=2) + '\n').encode())
        return 0
    except (OSError, ValueError, ssa.probe.ProbeError, subprocess.SubprocessError) as error:
        print('native stream coverage: ' + str(error))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
