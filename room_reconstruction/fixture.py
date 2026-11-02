"""Synthetic benchmark fixture, never eligible for a headset room capture."""
from pathlib import Path
import math
import random
import struct
import zlib

from .backend import write_json


def png(width, height, pixels):
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    scan = b''.join(b'\0'+pixels[row*width*3:(row+1)*width*3] for row in range(height))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))
            +chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))


def create(directory, width=160, height=120, views=16):
    root = Path(directory)
    root.mkdir(mode=0o700)
    (root/'images').mkdir(mode=0o700)
    focal = width*.7
    frames, points = [], []
    rng = random.Random(81726)
    for index in range(views):
        yaw = (index-(views-1)/2)*.045
        cosine, sine = math.cos(yaw), math.sin(yaw)
        eye = -.032 if index%2 == 0 else .032
        origin = ((index-(views-1)/2)*.035+eye, 1.5, .1)
        pixels = bytearray(width*height*3)
        for v in range(height):
            for u in range(width):
                x, y = (u-(width-1)/2)/focal, -(v-(height-1)/2)/focal
                direction = (cosine*x-sine,y,-sine*x-cosine)
                nearest, position, color = 1000., None, (0,0,0)
                for axis, boundary, base in ((1,0.,(110,85,55)),(2,-3.,(75,145,185)),
                                             (0,-2.,(165,95,115)),(0,2.,(105,160,95))):
                    if abs(direction[axis]) < 1e-9:
                        continue
                    distance = (boundary-origin[axis])/direction[axis]
                    if not 0 < distance < nearest:
                        continue
                    point = tuple(origin[k]+distance*direction[k] for k in range(3))
                    if not 0 <= point[1] <= 3 or abs(point[0]) > 2.000001 or point[2] < -3.000001:
                        continue
                    a, b = (k for k in range(3) if k != axis)
                    checker = (math.floor(point[a]*9)+math.floor(point[b]*9))%2
                    detail = .72+.25*checker+.03*math.sin(point[a]*31+point[b]*47)
                    color = tuple(max(0,min(255,int(value*detail))) for value in base)
                    nearest, position = distance, point
                offset = (v*width+u)*3
                pixels[offset:offset+3] = bytes(color)
                if position and u%6 == 0 and v%6 == 0:
                    # An explicitly noisy sparse seed mimics an imperfect
                    # initialization. This is not measured room geometry.
                    points.append((*[coordinate+rng.gauss(0,.05) for coordinate in position],*color))
        name = f'images/{index:04d}.png'
        (root/name).write_bytes(png(width,height,pixels))
        frames.append({'file_path':name,'transform_matrix':[
            [cosine,0.,sine,origin[0]],[0.,1.,0.,origin[1]],[-sine,0.,cosine,origin[2]],[0.,0.,0.,1.]]})
    cloud = root/'seed.ply'
    with cloud.open('x',encoding='ascii') as output:
        output.write('ply\nformat ascii 1.0\nelement vertex '+str(len(points))+'\n'
                     'property float x\nproperty float y\nproperty float z\n'
                     'property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n')
        for point in points:
            output.write(' '.join(str(value) for value in point)+'\n')
    write_json(root/'transforms.json', {'camera_model':'PINHOLE','w':width,'h':height,
        'fl_x':focal,'fl_y':focal,'cx':(width-1)/2,'cy':(height-1)/2,'ply_file_path':'seed.ply','frames':frames})
    write_json(root/'fixture.json', {'test_only':True,'purpose':'PC backend benchmark',
        'known_floor_y':0.,'known_back_wall_z':-3.,'seed_noise_sigma_m':.05,
        'seed_points':len(points),'headset_capture':False,'real_room_accuracy_measured':False})
    return root
