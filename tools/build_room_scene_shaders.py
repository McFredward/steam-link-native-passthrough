#!/usr/bin/env python3
"""Compile original room-scene shaders into a bounded embedded public-API asset."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

def main():
    if len(sys.argv)!=2:
        raise SystemExit('Usage: build_room_scene_shaders.py <output-directory>')
    root=Path(__file__).resolve().parents[1]
    output=Path(sys.argv[1]).resolve();output.mkdir(parents=True,exist_ok=True)
    blocks=['// Generated from the original room_scan/scene shaders.','#pragma once','#include <cstdint>']
    records={}
    for stage,name in (('vert','RoomSceneVertexSpv'),('frag','RoomSceneFragmentSpv')):
        source=root/'room_scan'/('scene.'+stage);binary=output/('room-scene.'+stage+'.spv')
        subprocess.run(['glslc','--target-env=vulkan1.1',str(source),'-o',str(binary)],check=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(binary)],check=True)
        raw=binary.read_bytes()
        if not raw or len(raw)>65536 or len(raw)%4:raise ValueError('Room shader exceeds budget')
        words=[int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)]
        blocks.append('inline constexpr std::uint32_t '+name+'[]={'+','.join(hex(x) for x in words)+'};')
        records[stage]={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
    (output/'room_scene_shaders.h').write_text('\n'.join(blocks)+'\n')
    (output/'room-scene-shaders.json').write_text(json.dumps(records,indent=2)+'\n')
    print('Validated and embedded original room-scene vertex and fragment shaders.')

if __name__=='__main__':main()
