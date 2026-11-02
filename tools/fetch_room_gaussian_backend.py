#!/usr/bin/env python3
"""Fetch the unmodified pinned PC trainer into a private cache; never install it."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import tempfile
import urllib.request
import zipfile


def fetch(target, pin):
    if target.exists():
        if target.stat().st_size == pin['bytes'] and hashlib.sha256(target.read_bytes()).hexdigest() == pin['sha256']:
            return
        raise ValueError('Cached reconstruction input differs from its pin: '+target.name)
    request = urllib.request.Request(pin['url'],headers={'User-Agent':'SteamLinkNativePassthrough-room-reconstruction'})
    temporary = target.with_suffix(target.suffix+'.part')
    try:
        count = 0
        checksum = hashlib.sha256()
        with urllib.request.urlopen(request,timeout=45) as source, temporary.open('xb') as output:
            while block := source.read(1024*1024):
                count += len(block)
                if count > pin['bytes']:
                    raise ValueError('Reconstruction input exceeds its pinned size')
                checksum.update(block)
                output.write(block)
        if count != pin['bytes'] or checksum.hexdigest() != pin['sha256']:
            raise ValueError('Reconstruction input does not match its pin')
        temporary.chmod(0o600)
        os.replace(temporary,target)
    finally:
        temporary.unlink(missing_ok=True)


def prepare(cache, platform):
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root/'packaging/room-gaussian-backend.json').read_text())
    cache = Path(cache).absolute()
    cache.mkdir(parents=True,exist_ok=True,mode=0o700)
    pin = manifest['platforms'][platform]
    archive = cache/(platform+'.zip')
    fetch(archive,pin)
    fetch(cache/'Spirula-LICENSE.txt',manifest['license'])
    binary = cache/pin['binary']['name']
    with zipfile.ZipFile(archive) as source:
        names = source.namelist()
        if names != [pin['binary']['name']]:
            raise ValueError('Unexpected files in the native backend archive')
        item = source.getinfo(names[0])
        if item.file_size != pin['binary']['bytes']:
            raise ValueError('Native backend extent differs from its pin')
        fd, name = tempfile.mkstemp(prefix='backend.',dir=cache)
        temporary = Path(name)
        try:
            with os.fdopen(fd,'wb') as output, source.open(item) as data:
                shutil.copyfileobj(data,output,1024*1024)
            checksum = hashlib.sha256()
            with temporary.open('rb') as data:
                for block in iter(lambda:data.read(1024*1024),b''):
                    checksum.update(block)
            if checksum.hexdigest() != pin['binary']['sha256']:
                raise ValueError('Native backend identity differs from its pin')
            temporary.chmod(0o700)
            os.replace(temporary,binary)
        finally:
            temporary.unlink(missing_ok=True)
    return binary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cache',type=Path)
    parser.add_argument('--platform',choices=('windows','linux_development'),default='windows')
    options = parser.parse_args()
    print(prepare(options.cache,options.platform))
