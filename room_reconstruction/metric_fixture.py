"""Known-scale synthetic room for geometry tests; never a real-room capture."""
from pathlib import Path
import numpy as np

from .backend import write_json
from .fixture import png


def create(directory, width=128, height=96, seed_ceiling_bias=.12):
    root = Path(directory); root.mkdir(mode=0o700)
    (root/'images').mkdir(mode=0o700)
    focal = width*.7; ceiling = 2.7
    y, x = np.mgrid[:height, :width]
    local = np.stack(((x-(width-1)/2)/focal, -(y-(height-1)/2)/focal, -np.ones_like(x)), -1)
    rng = np.random.default_rng(2700)
    frames, seeds, witnesses = [], [], []
    planes = [(0, -2., (170, 110, 100)), (0, 2., (100, 170, 120)),
              (1, 0., (150, 125, 90)), (1, ceiling, (180, 180, 175)),
              (2, -2., (100, 135, 180)), (2, 2., (170, 150, 110))]
    for yaw in np.arange(4)*np.pi/2:
        for pitch in (-.7, 0., .7):
            forward = np.array((np.sin(yaw)*np.cos(pitch), np.sin(pitch), -np.cos(yaw)*np.cos(pitch)))
            right = np.cross(forward, (0, 1, 0)); right /= np.linalg.norm(right)
            up = np.cross(right, forward)
            rotation = np.column_stack((right, up, -forward))
            centre = np.array((.3*np.cos(yaw), 1.35, .3*np.sin(yaw)))
            rays = local @ rotation.T
            for eye in (-.032, .032):
                origin = centre+eye*right
                nearest = np.full((height, width), np.inf)
                points = np.zeros((height, width, 3)); colors = np.zeros((height, width, 3), np.uint8)
                ceiling_hit = np.zeros((height, width), bool)
                for axis, boundary, color in planes:
                    denominator = rays[..., axis]
                    distance = np.divide(boundary-origin[axis], denominator,
                                         out=np.full_like(denominator, np.inf), where=abs(denominator)>1e-9)
                    candidate = origin+np.where(np.isfinite(distance), distance, 0)[..., None]*rays
                    valid = (distance>0)&(distance<nearest)
                    valid &= (abs(candidate[..., 0])<=2.000001)&(abs(candidate[..., 2])<=2.000001)
                    valid &= (candidate[..., 1]>=-1e-6)&(candidate[..., 1]<=ceiling+1e-6)
                    a, b = [i for i in range(3) if i != axis]
                    detail = (.78+.10*np.sin(candidate[..., a]*17+candidate[..., b]*11)
                              +.08*np.cos(candidate[..., a]*29-candidate[..., b]*23)
                              +.04*np.sin(candidate[..., a]*53+candidate[..., b]*41))
                    colors[valid] = np.clip(detail[..., None]*color, 0, 255).astype(np.uint8)[valid]
                    points[valid] = candidate[valid]; nearest[valid] = distance[valid]
                    ceiling_hit[valid] = axis==1 and boundary==ceiling
                good = np.isfinite(nearest)
                measured = points[::8, ::8][good[::8, ::8]]
                rgb = colors[::8, ::8][good[::8, ::8]]
                initialized = measured+rng.normal(0, .02, measured.shape)
                initialized[ceiling_hit[::8, ::8][good[::8, ::8]], 1] += seed_ceiling_bias
                seeds.extend((*p, *map(int, c)) for p, c in zip(initialized, rgb))
                witnesses.extend(points[::4, ::4][good[::4, ::4]])
                name = f'images/{len(frames):04d}.png'
                (root/name).write_bytes(png(width, height, colors.tobytes()))
                transform = np.eye(4); transform[:3, :3] = rotation; transform[:3, 3] = origin
                frames.append({'file_path': name, 'transform_matrix': transform.tolist()})
    with (root/'seed.ply').open('x', encoding='ascii') as output:
        output.write('ply\nformat ascii 1.0\nelement vertex '+str(len(seeds))+'\n'
                     'property float x\nproperty float y\nproperty float z\n'
                     'property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n')
        for point in seeds:
            output.write(' '.join(map(str, point))+'\n')
    write_json(root/'transforms.json', {'camera_model':'PINHOLE', 'w':width, 'h':height,
        'fl_x':focal, 'fl_y':focal, 'cx':(width-1)/2, 'cy':(height-1)/2,
        'ply_file_path':'seed.ply', 'frames':frames})
    witness = np.asarray(witnesses)
    _, indices = np.unique(np.floor(witness/.075).astype(np.int32), axis=0, return_index=True)
    np.savez_compressed(root/'ground-truth.npz', points=witness[indices], ceiling_height_m=ceiling)
    write_json(root/'fixture.json', {'test_only':True, 'headset_capture':False,
        'purpose':'Independent metric accuracy and observed-surface completeness test',
        'room_bounds_m':[[-2,0,-2],[2,ceiling,2]], 'seed_ceiling_bias_m':seed_ceiling_bias,
        'seed_noise_sigma_m':.02, 'seed_points':len(seeds), 'camera_views':len(frames),
        'ground_truth_supplied_to_optimizer':False})
    return root
