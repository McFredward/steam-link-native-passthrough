#!/usr/bin/env python3
"""Check actual packaged setup derivation against private native references.

This never connects to a device or writes runtime files. Native input and
derived shader bytes remain private and are not included in the report.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import types
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',required=True,type=Path)
    parser.add_argument('--stable',required=True,type=Path)
    parser.add_argument('--current',required=True,type=Path)
    args=parser.parse_args()
    with zipfile.ZipFile(args.package) as archive:
        files={name:archive.read(name) for name in archive.namelist()}
    manifest=json.loads(files['bootstrap-package.json'])
    import hashlib
    assert manifest['files']=={name:{'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
                              for name,raw in files.items() if name!='bootstrap-package.json'}
    with tempfile.TemporaryDirectory(prefix='sfnp-setup-generation-') as temporary:
        root=Path(temporary)
        for name in ('setup/steam_link_setup.py','setup/stream_bridge_install.py'):
            path=root/name;path.parent.mkdir(exist_ok=True);path.write_bytes(files[name])
        engine=load('checked_generation_engine',root/'setup/steam_link_setup.py')
        bridge=load('checked_generation_bridge',root/'setup/stream_bridge_install.py')
        bundle=types.SimpleNamespace(frame=json.loads(files['stream-bridge-bundle.json'])['frame'],files=files)
        for name,path,expected in [('stable',args.stable,bridge.GENERATED),('current',args.current,bridge.GENERATED_CURRENT)]:
            stage=root/name;stage.mkdir()
            outputs=bridge.generate_shaders(engine,bundle,path.read_bytes(),stage)
            assert {key:{'bytes':len(raw),'sha256':engine.sha(raw)} for key,raw in outputs.items()}==expected
            print(name+': actual packaged shader preparation PASS')


if __name__=='__main__':main()
