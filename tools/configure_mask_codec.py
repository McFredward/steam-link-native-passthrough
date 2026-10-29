#!/usr/bin/env python3
"""Select the PC mask codec for the next SteamVR start; never touches the Frame."""
import argparse
import os
from pathlib import Path
import sys

MARKER='SFNP_MASK_HEVC_V1\n'


def ordinary(path):
    for current in (path,*path.parents):
        if current.is_symlink() or (hasattr(current,'is_junction') and current.is_junction()):
            raise RuntimeError('Linked configuration paths are not supported')
    if path.exists() and (not path.is_file() or path.stat().st_nlink!=1):
        raise RuntimeError('Expected a regular single-link configuration file')


def configure(root,rate=None):
    if not root.is_dir():raise RuntimeError('Install the PC companion first')
    target=root/'mask-codec.conf';ordinary(target)
    previous=target.read_bytes() if target.exists() else None
    if previous is not None and (len(previous)>64 or not previous.startswith(MARKER.encode('ascii'))):
        raise RuntimeError('Existing unrelated configuration was preserved')
    # Absence now selects HEVC by default. Persist zero so an explicit lossless
    # preference survives normal setup, updates and repeated SteamVR starts.
    if rate is None:rate=0
    if type(rate)is not int or (rate!=0 and not 1000000<=rate<=200000000):raise RuntimeError('Mask bitrate must be1–200 Mbit/s')
    pending=target.with_suffix('.tmp');ordinary(pending)
    try:
        with pending.open('xb') as stream:
            stream.write((MARKER+str(rate)+'\n').encode('ascii'));stream.flush();os.fsync(stream.fileno())
    except FileExistsError:
        raise RuntimeError('Existing configuration staging file was preserved') from None
    try:os.replace(pending,target)
    finally:
        if pending.exists():pending.unlink()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bitrate',type=int,default=20,help='Mask CBR target in Mbit/s (default20)')
    parser.add_argument('--disable',action='store_true',help='Restore the lossless mask route')
    parser.add_argument('--root',type=Path)
    args=parser.parse_args()
    if not 1<=args.bitrate<=200:parser.error('--bitrate must be1–200')
    if args.root is None and not os.environ.get('LOCALAPPDATA'):parser.error('Run on the Windows PC')
    configure(args.root or Path(os.environ['LOCALAPPDATA'])/'steam-link-native-passthrough',
              None if args.disable else args.bitrate*1000000)
    print('Lossless masks selected.' if args.disable else f'NVENC HEVC masks selected: {args.bitrate} Mbit/s target.')
    print('The selection takes effect on the next PC SteamVR start. No device was restarted.')


if __name__=='__main__':
    try:main()
    except (RuntimeError,OSError) as error:
        print('Mask configuration failed: '+str(error),file=sys.stderr);sys.exit(1)
