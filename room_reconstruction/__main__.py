"""PC prototype CLI. Benchmarking does not start or modify a Frame session."""
from dataclasses import asdict
import argparse
import json
from pathlib import Path
import sys

from . import backend, fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command',required=True)
    sub.add_parser('probe',help='Read NVIDIA GPU memory and choose a bounded initial profile')
    for name in ('benchmark','train','mesh'):
        job = sub.add_parser(name)
        job.add_argument('--backend',type=Path,required=True)
        job.add_argument('--output',type=Path,required=True,help='A new output directory; existing data is never replaced')
        job.add_argument('--manifest',type=Path,default=Path(__file__).resolve().parents[1]/'packaging/room-gaussian-backend.json')
        job.add_argument('--development',action='store_true',help=argparse.SUPPRESS)
        if name != 'mesh':
            job.add_argument('--steps',type=int,default=200 if name == 'benchmark' else 2000)
        if name == 'train':
            job.add_argument('--dataset',type=Path,required=True)
    args = parser.parse_args()
    gpu = None
    if not getattr(args,'development',False):
        candidates = backend.probe_nvidia()
        gpu = max(candidates,key=lambda value:value.free_mib)
        # The native device selector must name the same unambiguous GPU.
        backend.require(len(candidates)==1,'This first benchmark requires one NVIDIA GPU; explicit multi-GPU selection is not implemented yet')
        profile = backend.choose_profile(gpu)
    else:
        profile = backend.Profile('development-smoke',2000,128)
    if args.command == 'probe':
        print(json.dumps({'gpu':asdict(gpu),'profile':asdict(profile),'profile_status':'initial conservative budget; not a compatibility guarantee'}))
        return
    manifest = backend.read_json(args.manifest)
    if args.command == 'mesh':
        result = backend.mesh(args.backend,args.output,manifest=manifest,development=args.development)
        print(json.dumps({'mesh':str(result),'physical_accuracy_verified':False}))
        return
    output = args.output.absolute()
    output.parent.mkdir(parents=True,exist_ok=True)
    if args.command == 'benchmark':
        fixture_path = output.with_name(output.name+'-synthetic-input')
        dataset = fixture.create(fixture_path, width=128 if args.development else 160,
                                 height=96 if args.development else 120, views=12 if args.development else 16)
        # A first measured workload, not a claim that an entire room fits this budget.
        profile = backend.Profile('synthetic-benchmark',min(profile.max_splats,100000),profile.image_side)
    else:
        dataset = args.dataset
    print('Starting owned PC Gaussian trainer. Press Ctrl+C to stop; completed checkpoints are preserved.',flush=True)
    result = backend.train(args.backend,dataset,output,profile,args.steps,manifest=manifest,development=args.development)
    result['gpu'] = asdict(gpu) if gpu else {'mode':'development adapter; inspect training.log for the actual device'}
    result['synthetic_benchmark'] = args.command == 'benchmark'
    backend.write_json(output/'result.json',result)
    print(json.dumps(result),flush=True)


if __name__ == '__main__':
    try:
        main()
    except (backend.ReconstructionError,OSError,ValueError) as error:
        print('Room reconstruction: '+str(error),file=sys.stderr)
        raise SystemExit(1)
