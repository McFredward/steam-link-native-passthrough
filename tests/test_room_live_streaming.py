"""Continuous detailed capture, bounded streaming and metric ceiling refinement."""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import threading
import unittest
import zlib

import numpy as np
from scipy.ndimage import gaussian_filter,map_coordinates
from scipy.spatial.transform import Rotation

from room_reconstruction.live_coverage import DirectionCoverage
from room_reconstruction.live_geometry import triangulate
from room_reconstruction.live_refine import refine_stereo
from room_reconstruction.live_tiles import TiledGaussianMap
from room_scan.capture import Binding
from room_scan.geometry import RoomError
from room_scan.pc_reconstruction import PcReconstruction

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('room_stream_helper',ROOT/'setup/steam_link_setup_room.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
BINDING={'session_id':'00000000-0000-0000-0000-000000000001',
         'origin_id':'00000000-0000-0000-0000-000000000002',
         'calibration_id':'00000000-0000-0000-0000-000000000003'}


def chunk(key='a'*32,shift=0):
    points=((shift,0,-2),(shift+.1,0,-2),(shift,.1,-2))
    gaussians=b''.join(struct.pack('<I11f',i,*p,.01,0,0,0,.01,0,.6,1) for i,p in enumerate(points))
    vertices=b''.join(struct.pack('<I3fB3x',i,*p,1) for i,p in enumerate(points))
    return {'key':key,'cell':[0,0,-2],'gaussians':base64.b64encode(gaussians).decode(),
            'surface_vertices':base64.b64encode(vertices).decode(),
            'surface_triangles':base64.b64encode(struct.pack('<3I',0,1,2)).decode(),'face_count':1}


class StreamingTests(unittest.TestCase):
    @unittest.skipIf(os.name=='nt','The same-user Frame socket is tested on Linux')
    def test_capture_and_model_use_two_ssh_round_trips_instead_of_per_chunk_round_trips(self):
        from room_scan.pc_relay import PcRelay
        with tempfile.TemporaryDirectory() as directory:
            relay=PcRelay(Path(directory)/'relay');self.addCleanup(relay.close)
            worker='15'*16;calls=[];local=[]
            def dispatch(value):
                local.append(value['op']);return relay._dispatch(value)
            def exchange(value):
                calls.append(value['op']);return helper.stream_exchange({'worker_id':worker,**value},dispatch)
            self.assertEqual(exchange({'op':'ready','protocol':4,'provider':'CUDAExecutionProvider'})['stream_protocol'],1)
            relay.set_scan(BINDING,2,'scan','room-a')
            capture={'binding':BINDING,'capture_epoch':2,'pixels':base64.b64encode(os.urandom(450000)).decode()}
            relay.offer(capture);calls.clear();local.clear()
            state=exchange({'op':'take_all','seed':False})
            self.assertEqual(json.loads(zlib.decompress(base64.b64decode(state['packet']))),capture)
            companion=object.__new__(helper.Companion);companion.fast_stream=True;companion.stop=threading.Event();companion.exchange=exchange
            result={'binding':BINDING,'capture_epoch':2,'event':'pc_model','sequence':1,
                    'payload':base64.b64encode(os.urandom(250000)).decode()}
            self.assertTrue(companion._upload(result));self.assertEqual(relay.events.get_nowait(),result)
            self.assertEqual(calls,['take_all','batch'])
            self.assertGreater(local.count('chunk')+local.count('put'),10)
            pending={'binding':BINDING,'capture_epoch':2,'pixels':'next'};relay.offer(pending)
            with self.assertRaises(RoomError):
                relay._dispatch({'op':'ready','worker_id':'16'*16,'protocol':4,'provider':'CUDAExecutionProvider'})
            with self.assertRaises(ValueError):
                helper.stream_exchange({'op':'batch','worker_id':worker,'requests':[{'op':'offline'}]},dispatch)
            self.assertTrue(relay.ready);self.assertIsNotNone(relay.pending)

    @unittest.skipIf(os.name=='nt','The same-user Frame socket is tested on Linux')
    def test_full_result_queue_preserves_each_region_until_the_sender_retries(self):
        from room_scan.pc_relay import PcRelay
        with tempfile.TemporaryDirectory() as directory:
            relay=PcRelay(Path(directory)/'relay');self.addCleanup(relay.close)
            worker='15'*16
            exchange=lambda request:helper.stream_exchange({'worker_id':worker,**request},relay._dispatch)
            exchange({'op':'ready','protocol':4,'provider':'CUDAExecutionProvider'})
            relay.set_scan(BINDING,2,'scan','room-a')
            companion=object.__new__(helper.Companion);companion.fast_stream=True
            companion.stop=threading.Event();companion.exchange=exchange
            values=[{'binding':BINDING,'capture_epoch':2,'event':'pc_model','sequence':i,
                     'chunks':[chunk(str(i)*32)]} for i in (1,2,3)]
            self.assertTrue(companion._upload(values[0]));self.assertTrue(companion._upload(values[1]))
            self.assertIs(companion._upload(values[2]),False)
            self.assertEqual(relay.events.get_nowait(),values[0])
            self.assertTrue(companion._upload(values[2]))
            self.assertEqual(relay.events.get_nowait(),values[1])
            self.assertEqual(relay.events.get_nowait(),values[2])

    def test_region_updates_keep_stable_paths_and_reject_a_partial_invalid_batch(self):
        with tempfile.TemporaryDirectory() as directory:
            reconstruction=PcReconstruction(Binding(**BINDING,test_only=False),directory)
            self.addCleanup(reconstruction.close)
            def result(sequence,chunks):return {'event':'pc_model','binding':BINDING,'sequence':sequence,
                'chunks':chunks,'camera':[0,0,0],'map_points':sequence*3,'tile_count':len(chunks)}
            reconstruction.accept(result(1,[chunk()]))
            path=reconstruction.chunks['a'*32].surface_path
            reconstruction.accept(result(2,[chunk(shift=.01),chunk('b'*32,shift=.2)]))
            self.assertEqual(reconstruction.chunks['a'*32].surface_path,path)
            self.assertEqual(len(reconstruction.surface_tiles()),2)
            self.assertEqual(len(reconstruction.mesh().triangles),2)
            before=path.read_bytes();bad=chunk('c'*32);bad['surface_triangles']=base64.b64encode(struct.pack('<3I',0,1,9)).decode()
            with self.assertRaises(RoomError):reconstruction.accept(result(3,[chunk(shift=.02),bad]))
            self.assertEqual(path.read_bytes(),before);self.assertEqual(reconstruction.sequence,2)
            self.assertEqual(set(reconstruction.chunks),{'a'*32,'b'*32})
            reconstruction.accept({**result(3,[]),'removed':['a'*32]})
            self.assertFalse(path.exists());self.assertEqual(set(reconstruction.chunks),{'b'*32})

    def test_dashboard_does_not_rebuild_the_combined_room_mesh_for_live_regions(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        from room_scan.app import Application
        from room_scan.store import RoomStore
        with tempfile.TemporaryDirectory() as directory:
            app=Application(RoomStore(Path(directory)/'rooms'),reconstruction_factory=lambda b:PcReconstruction(b,directory))
            app.pc_relay=SimpleNamespace(lock=threading.Lock(),ready=True)
            app.action('new');app.event({'event':'camera_ready','binding':BINDING})
            app.event({'event':'pc_model','binding':BINDING,'sequence':1,'chunks':[chunk()],
                       'camera':[0,0,0],'map_points':3,'tile_count':1})
            with patch.object(app.session.reconstruction,'mesh',side_effect=AssertionError('live HUD expanded the full room')):
                view=app.view()
            self.assertIsNone(view['room']['mesh']);self.assertTrue(view['room']['can_review'])
            self.assertEqual(len(view['surface_tiles']),1);self.assertTrue(view['guidance']['active'])
            app.close()


class DetailAndDepthTests(unittest.TestCase):
    def test_pending_regions_drain_without_another_capture_and_survive_polygon_extraction(self):
        h,w,f=96,128,40.;y,x=np.mgrid[:h,:w]
        points=np.stack(((x-63.5)*2/f,-(y-47.5)*2/f,-np.full_like(x,2.)),-1)
        normals=np.zeros_like(points);normals[...,2]=1
        with tempfile.TemporaryDirectory() as directory:
            model=TiledGaussianMap(Path(directory)/'pc')
            model.integrate(points,normals,np.ones((h,w),bool),np.full((h,w),2.),np.zeros(3),f)
            reader=PcReconstruction(Binding(**BINDING,test_only=False),directory)
            try:
                serial=0
                while model.dirty or model.pending:
                    serial+=1;value=model.snapshot()
                    self.assertLess(serial,20)
                    reader.accept({'event':'pc_model','sequence':serial,'binding':BINDING,**value})
                self.assertGreater(serial,1);self.assertGreater(len(reader.chunks),64)
                mesh=model.mesh();self.assertTrue(mesh['triangles'])
                x_values=np.asarray(mesh['vertices'])[:,0]
                self.assertLess(float(x_values.min()),-2);self.assertGreater(float(x_values.max()),2)
                restored=TiledGaussianMap(Path(directory)/'pc')
                self.assertEqual(set(restored.index),set(model.index))
            finally:reader.close()

    def test_revisits_correct_false_foreground_across_regions_and_preserve_occluded_background(self):
        h,w,f=48,64,80.;y,x=np.mgrid[:h,:w];camera=np.array((.5,.5,.2))
        rays=np.stack(((x-31.5)/f,-(y-23.5)/f,-np.ones_like(x)),-1)
        normals=np.zeros_like(rays);normals[...,2]=1;valid=np.ones((h,w),bool)
        room=np.column_stack((np.eye(3),camera))
        with tempfile.TemporaryDirectory() as directory:
            model=TiledGaussianMap(directory)
            def observe(distance):
                model.integrate(camera+rays*distance,normals,valid,np.full((h,w),distance),camera,f,room)
                return model.snapshot()
            observe(.7)
            foreground={key:item['points'] for key,item in model.index.items()}
            for _ in range(3):observe(2.)
            self.assertLess(sum(model.index[k]['points'] for k in foreground),sum(foreground.values())*.3)
            background={key:item['points'] for key,item in model.index.items() if key not in foreground}
            for _ in range(4):observe(.7)
            self.assertEqual({key:model.index[key]['points'] for key in background},background)

    def test_more_than_the_old_room_capacity_is_acquired_and_restored_after_resident_eviction(self):
        h,w,f=192,256,400.;y,x=np.mgrid[:h,:w]
        base=np.stack(((x-127.5)*.7/f,-(y-95.5)*.7/f,-np.full_like(x,.7)),-1)
        normals=np.zeros_like(base);normals[...,2]=1
        with tempfile.TemporaryDirectory() as directory:
            model=TiledGaussianMap(directory,resident=2)
            for i in range(64):
                camera=np.array((i+.5,.5,0.));points=base+camera
                model.integrate(points,normals,np.ones((h,w),bool),np.full((h,w),.7),camera,f)
                live=model.snapshot()
                self.assertFalse(live['at_capacity']);self.assertTrue(live['chunks'])
                self.assertLessEqual(len(model.models),2)
            self.assertGreater(live['map_points'],240000)
            model.flush();first_key=next(iter(model.index));saved=model._model(first_key).positions.copy()
            restored=TiledGaussianMap(directory,resident=2)
            self.assertTrue(np.array_equal(restored._model(first_key).positions,saved))
            self.assertEqual(sum(v['points'] for v in restored.index.values()),live['map_points'])
            self.assertTrue(all(not value.dtype.hasobject for value in [restored._model(first_key).positions]))

    def test_changed_checkpoint_is_refused_without_overwriting_it(self):
        h=w=16;f=80.;y,x=np.mgrid[:h,:w]
        points=np.stack(((x-7.5)/f+.5,-(y-7.5)/f+.5,-np.ones_like(x)),-1)
        normals=np.zeros_like(points);normals[...,2]=1
        with tempfile.TemporaryDirectory() as directory:
            model=TiledGaussianMap(directory);model.integrate(points,normals,np.ones((h,w),bool),np.ones((h,w)),np.array((.5,.5,0)),f)
            model.flush();key=next(iter(model.index));path=Path(directory)/(key+'.npz');raw=path.read_bytes()+b'changed';path.write_bytes(raw)
            other=TiledGaussianMap(directory)
            with self.assertRaisesRegex(ValueError,'Changed PC room checkpoint'):other._model(key)
            self.assertEqual(path.read_bytes(),raw)

    def test_photometric_refinement_corrects_a_false_high_ceiling_during_a_sloping_view(self):
        rng=np.random.default_rng(1315);h,w,f,baseline=96,160,120.,.064;y,x=np.mgrid[:h,:w]
        local=np.stack(((x-79.5)/f,-(y-47.5)/f,-np.ones_like(x)),-1)
        rotation=Rotation.from_euler('x',50,degrees=True).as_matrix();rays=local@rotation.T
        true_disparity=f*baseline*rays[...,1]/1.1
        left=gaussian_filter(rng.uniform(0,255,(h,w)),.5).astype(np.uint8)
        right=map_coordinates(left.astype(float),[y,x+true_disparity],order=1,mode='nearest').astype(np.uint8)
        masks=[np.ones((h,w),bool)]*2;origins=[np.zeros_like(rays),np.broadcast_to((baseline,0,0),rays.shape)]
        room=np.column_stack((rotation,np.zeros(3)));proposal=(true_disparity-1.2,true_disparity-1.2)
        before,_,valid_before,_=triangulate(proposal,masks,origins,rays,room,(f,f,79.5,47.5))
        disparity,qualified=refine_stereo((left,right),proposal,masks)
        after,_,valid,_=triangulate(disparity,qualified,origins,rays,room,(f,f,79.5,47.5),lr_tolerance=.35)
        self.assertGreater(int(valid.sum()),7000)
        self.assertGreater(float(np.median(abs(before[valid_before,1]-1.1))),.25)
        self.assertLess(float(np.median(abs(after[valid,1]-1.1))),.02)
        self.assertLess(float(np.quantile(abs(after[valid,1]-1.1),.95)),.04)

    def test_blank_unsupported_ceiling_does_not_become_a_confident_false_plane(self):
        image=np.full((48,64),116,np.uint8);mask=np.ones_like(image,bool);disparity=np.full(image.shape,8,np.float32)
        _,qualified=refine_stereo((image,image),(disparity,disparity),(mask,mask))
        self.assertFalse(qualified[0].any());self.assertFalse(qualified[1].any())
        y,x=np.mgrid[:48,:64];rays=np.stack(((x-31.5)/80,-(y-23.5)/80,-np.ones_like(x)),-1)
        coverage=DirectionCoverage().update(rays,mask,np.zeros_like(mask))
        self.assertGreater(sum(coverage['observed']),0);self.assertEqual(sum(coverage['measured']),0)


if __name__=='__main__':unittest.main()
