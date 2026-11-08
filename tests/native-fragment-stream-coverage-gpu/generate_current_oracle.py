#!/usr/bin/env python3
"""Private GPU oracle: export consumed native branch outcomes and blend weight.

This is an inert test instrument, never an installed shader. Original shader
instructions come exclusively from the caller's private input.
"""
import argparse
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import native_fragment_stream_coverage as coverage


def generate(raw):
    raw=coverage.shader_contract._normalize(raw,coverage.shader_contract._METADATA['fragment_beta'])
    words=struct.unpack('<%dI'%(len(raw)//4),raw);header=list(words[:5]);native=[];at=5
    while at<len(words):
        n=words[at]>>16;native.append(coverage.ssa.Instruction(words[at]&65535,tuple(words[at+1:at+n])));at+=n
    make=lambda op,*args:coverage.ssa.Instruction(op,tuple(args))
    phis={660:header[3],661:header[3]+1};output=header[3]+2;result=[];stores=0
    for item in native:
        if item.opcode==245 and item.operands[1] in phis:
            pairs=list(item.operands[2:])
            for n in range(0,len(pairs),2):pairs[n]=94 if pairs[n]==210 else 68
            result.append(make(245,6,phis[item.operands[1]],*pairs))
        if item.opcode==62 and item.operands==(337,650):
            result.append(make(80,27,output,phis[660],phis[661],665,511));item=make(62,337,output);stores+=1
        result.append(item)
    if stores!=1:raise ValueError('single current native output required')
    header[3]=output+1;words=header+[word for item in result for word in item.words()]
    return struct.pack('<%dI'%len(words),*words)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original',required=True,type=Path);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    with args.output.open('xb') as f:f.write(generate(args.original.read_bytes()))
