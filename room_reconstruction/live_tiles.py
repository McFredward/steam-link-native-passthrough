"""Spatial Gaussian submaps with private PC checkpoints and incremental previews."""
from __future__ import annotations

import base64
from collections import OrderedDict
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import zipfile

import numpy as np
from scipy.ndimage import minimum_filter

from .live_map import GaussianMap, SurfaceBudgetError

FIELDS=('positions','normals','sigma','weights','views','viewpoints','residuals','active','occluded')


class TiledGaussianMap:
    """A display budget never becomes a total-room acquisition budget."""
    def __init__(self, root, *, step=2, resident=48):
        self.root=Path(root)
        if any(path.is_symlink() for path in (self.root,*self.root.parents)):
            raise ValueError('PC room storage cannot contain links')
        self.root.mkdir(parents=True,exist_ok=True,mode=0o700)
        self.step,self.resident=step,resident
        self.models=OrderedDict();self.index={};self.dirty=set();self.frames=0
        self.capacity_limited=False;self.camera=np.zeros(3);self.forward=np.array((0.,0.,-1.))
        self._first_snapshot=True
        self.coverage={'observed':[0]*72,'measured':[0]*72}
        path=self.root/'index.json'
        if path.exists():
            raw=self._read(path,2*1024*1024)
            value=json.loads(raw)
            if value.get('schema')!='sfnp-room-tiles-v1' or type(value.get('tiles'))is not dict:
                raise ValueError('Unknown PC room tile index')
            self.index=value['tiles']
            self.coverage=value.get('coverage',self.coverage)
            for key,item in self.index.items():
                if len(key)!=32 or any(c not in '0123456789abcdef' for c in key):raise ValueError('Invalid PC room tile key')
                if (type(item.get('cell'))is not list or len(item['cell'])!=3
                        or any(type(v)is not int or abs(v)>1001 for v in item['cell'])):
                    raise ValueError('Invalid PC room tile extent')
            self.dirty.update(self.index)
        self.pending=OrderedDict();self.part_ids={};self.announced={}

    @staticmethod
    def _read(path,limit):
        if path.is_symlink():raise ValueError('PC room checkpoint link refused')
        with path.open('rb') as stream:
            info=os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or not 0<info.st_size<=limit:
                raise ValueError('PC room checkpoint exceeds its extent')
            raw=stream.read(limit+1)
        if len(raw)!=info.st_size:raise ValueError('PC room checkpoint changed')
        return raw

    def _write(self,path,raw):
        if path.is_symlink():raise ValueError('Foreign PC room checkpoint preserved')
        fd,name=tempfile.mkstemp(prefix='.tile-',dir=self.root)
        try:
            with os.fdopen(fd,'wb') as stream:stream.write(raw);stream.flush();os.fsync(stream.fileno())
            os.replace(name,path)
        finally:Path(name).unlink(missing_ok=True)

    def _save(self,key,model):
        path=self.root/(key+'.npz')
        old=self.index[key].get('sha256')
        if path.exists() and (old is None or hashlib.sha256(self._read(path,32*1024*1024)).hexdigest()!=old):
            raise ValueError('Changed PC room checkpoint preserved')
        output=io.BytesIO()
        np.savez_compressed(output,**{name:getattr(model,name) for name in FIELDS},
                            faces=np.asarray(sorted(model.faces),dtype=np.int32).reshape(-1,3),
                            meta=np.array((model.frames,model.recycled,model.coarsenings),dtype=np.int64))
        raw=output.getvalue()
        if len(raw)>32*1024*1024:raise ValueError('PC tile checkpoint exceeds its extent')
        self._write(path,raw)
        self.index[key].update(sha256=hashlib.sha256(raw).hexdigest(),points=int(model.active.sum()),
                               faces=len(model.faces),frames=model.frames)
        self._write(self.root/'index.json',json.dumps({'schema':'sfnp-room-tiles-v1',
                    'tiles':{key:value for key,value in self.index.items() if value.get('sha256')},'coverage':self.coverage},
                    separators=(',',':'),allow_nan=False).encode())

    def _model(self,key):
        if key in self.models:
            self.models.move_to_end(key);return self.models[key]
        while len(self.models)>=self.resident:
            oldest,model=self.models.popitem(last=False);self._save(oldest,model)
        model=GaussianMap(80000,self.step,cell_m=.006,min_tangent=.004,compact=False)
        path=self.root/(key+'.npz')
        if path.exists():
            raw=self._read(path,32*1024*1024)
            if hashlib.sha256(raw).hexdigest()!=self.index[key].get('sha256'):
                raise ValueError('Changed PC room checkpoint preserved')
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                if len(archive.infolist())!=len(FIELDS)+2 or sum(f.file_size for f in archive.infolist())>64*1024*1024:
                    raise ValueError('PC room checkpoint expansion refused')
            with np.load(io.BytesIO(raw),allow_pickle=False) as arrays:
                if set(arrays)!=set(FIELDS)|{'faces','meta'}:raise ValueError('Invalid PC room checkpoint arrays')
                count=len(arrays['positions'])
                if count>320000:raise ValueError('PC room tile point extent changed')
                for name in FIELDS:
                    value=arrays[name]
                    shape=(count,3) if name in ('positions','normals','viewpoints') else (count,)
                    dtype=np.bool_ if name=='active' else np.uint8 if name in ('views','occluded') else np.float32
                    if value.shape!=shape or value.dtype!=dtype or not np.isfinite(value).all():
                        raise ValueError('Invalid PC room tile data')
                    setattr(model,name,value.copy())
                faces=arrays['faces'];meta=arrays['meta']
                if (faces.dtype!=np.int32 or faces.ndim!=2 or faces.shape[1]!=3 or len(faces)>640000
                        or faces.size and (faces.min()<0 or faces.max()>=count)
                        or meta.shape!=(3,) or meta.dtype!=np.int64 or (meta<0).any()
                        or (abs(model.positions)>1000).any()):raise ValueError('Invalid PC room tile connectivity')
                model.faces={tuple(map(int,f)) for f in faces}
                model.frames,model.recycled,model.coarsenings=map(int,meta)
        self.models[key]=model
        return model

    def integrate(self,points,normals,valid,depth,camera,focal,room=None):
        self.camera=np.asarray(camera);self.forward=-room[:,:3][:,2] if room is not None else self.forward
        y,x=np.nonzero(valid[::self.step,::self.step]);y*=self.step;x*=self.step
        if not len(x):return
        cells=np.floor(points[y,x]).astype(np.int32)
        unique,inverse=np.unique(cells,axis=0,return_inverse=True)
        lower=np.full((len(unique),2),max(valid.shape),np.int32);upper=np.zeros_like(lower)
        for axis,values in enumerate((y,x)):
            np.minimum.at(lower[:,axis],inverse,values);np.maximum.at(upper[:,axis],inverse,values)
        height,width=valid.shape
        minimum_depth=minimum_filter(np.where(valid,depth,0),size=3,mode='constant') if room is not None else None
        updated=set()
        for cell,lo,hi in zip(unique,lower,upper):
            key=hashlib.sha256(np.asarray(cell,dtype='<i4').tobytes()).hexdigest()[:32]
            if key not in self.index and len(self.index)>=8192:raise ValueError('PC room extent exceeds supported coordinates')
            self.index.setdefault(key,{'cell':cell.tolist(),'points':0,'faces':0,'frames':0})
            model=self._model(key)
            updated.add(key)
            y0,x0=np.maximum(0,lo-8)//self.step*self.step
            y1,x1=np.minimum((height,width),hi+10)
            crop=np.s_[y0:y1,x0:x1];p=points[crop]
            mask=valid[crop]&np.all((p>=cell-.16)&(p<=cell+1.16),axis=-1)
            model.integrate(p,normals[crop],mask,depth[crop],camera,focal,room,
                            principal=((width-1)/2-x0,(height-1)/2-y0),visibility=valid[crop],
                            minimum_depth=minimum_depth[crop] if minimum_depth is not None else None)
            self.index[key].update(points=int(model.active.sum()),faces=len(model.faces),frames=model.frames)
            self.dirty.add(key)
        if room is not None:
            # A farther measured wall can disprove an old foreground tile even
            # when no new points belong to that foreground's spatial cell.
            for key,item in list(self.index.items()):
                if key in updated or not item['points']:continue
                center=(np.asarray(item['cell'])+.5-room[:,3])@room[:,:3]
                z=-center[2]
                if z+.9<.25 or z-.9>6 or abs(center[0])>max(.25,z)*width/(2*focal)+.9 or abs(center[1])>max(.25,z)*height/(2*focal)+.9:continue
                model=self._model(key)
                if model.observe_visibility(depth,valid,room,focal,minimum_depth=minimum_depth):
                    item['points']=int(model.active.sum());self.dirty.add(key)
        self.frames+=1

    def snapshot(self):
        # Keep live meshing local. No global QEM/plane-union pass is allowed
        # to block the next moving stereo observation.
        for key in sorted(self.dirty,key=lambda k:np.linalg.norm(np.asarray(self.index[k]['cell'])+.5-self.camera)):
            model=self._model(key)
            faces=np.asarray(list(model.faces),dtype=np.int32).reshape(-1,3)
            if len(faces):
                faces=faces[model.active[faces].all(axis=1)]
                p=model.positions[faces]
                faces=faces[np.linalg.norm(np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]),axis=1)>1e-7]
            parts={}
            quality=model.quality()
            def partition(source,side=.25):
                if not len(source):return
                centers=model.positions[source].mean(axis=1)
                cells,inverse=np.unique(np.floor(centers/side).astype(np.int32),axis=0,return_inverse=True)
                order=np.argsort(inverse,kind='stable');starts=np.r_[0,np.flatnonzero(np.diff(inverse[order]))+1,len(order)]
                for cell,start,end in zip(cells,starts[:-1],starts[1:]):
                    local=source[order[start:end]];selected,ids=np.unique(local,return_inverse=True)
                    if len(selected)>80000 or len(local)>160000:
                        if side<.001:raise ValueError('Invalid overlapping room topology')
                        partition(local,side/2);continue
                    token=hashlib.sha256((key+':'+str(side)+':'+','.join(map(str,cell))).encode()).hexdigest()[:32]
                    vertices=np.zeros(len(selected),dtype=np.dtype([('id','<u4'),('p','<f4',(3,)),('q','u1'),('pad','u1',(3,))]))
                    vertices['id']=selected;vertices['p']=model.positions[selected];vertices['q']=quality[selected]
                    points=model.positions[selected];center=(points.min(axis=0)+points.max(axis=0))*.5
                    parts[token]={'key':token,'cell':self.index[key]['cell'],'center':center.tolist(),
                        'gaussians':model._records(selected,quality[selected]),'surface_vertices':base64.b64encode(vertices.tobytes()).decode(),
                        'surface_triangles':base64.b64encode(np.asarray(ids.reshape(-1,3),dtype='<u4').tobytes()).decode(),
                        'face_count':len(local)}
            partition(faces)
            previous=self.part_ids.get(key,set())
            for old in previous-set(parts):self.pending.pop(old,None)
            self.part_ids[key]=set(parts)
            for token,part in parts.items():self.pending[token]=(key,part)
            if model.frames%8==0:self._save(key,model)
        self.dirty.clear()
        chunks=[];removed=[];budget=0
        for token,(key,part) in list(self.pending.items()):
            size=sum(len(part.get(k,'')) for k in ('gaussians','surface_vertices','surface_triangles'))
            if budget+size>10*1024*1024 or len(chunks)>=64:break
            chunks.append(part);budget+=size;self.pending.pop(token)
            self.announced.setdefault(key,set()).add(token)
        for key,current in self.part_ids.items():
            if any(token in self.pending for token in current):continue
            old=self.announced.get(key,set());removed.extend(sorted(old-current));self.announced[key]=set(current)
        return {'chunks':chunks,'removed':removed,'map_points':sum(v['points'] for v in self.index.values()),
                'map_capacity':0,'recycled_points':sum(m.recycled for m in self.models.values()),
                'coarsenings':sum(m.coarsenings for m in self.models.values()),'tile_count':len(self.index),
                'preview_pending':bool(self.pending),'at_capacity':False,'camera':self.camera.tolist()}

    def flush(self):
        for key,model in list(self.models.items()):self._save(key,model)

    def seed(self,raw):
        model=GaussianMap(80000,self.step,cell_m=.006,min_tangent=.004,compact=False)
        model.restore_surface(raw)
        p=model.positions[model.active]
        if not len(p):return
        key=hashlib.sha256(b'retained-source').hexdigest()[:32]
        self.index[key]={'cell':np.floor(p.mean(axis=0)).astype(int).tolist(),'points':int(model.active.sum()),
                         'faces':len(model.faces),'frames':0}
        self.models[key]=model;self.dirty.add(key)

    def mesh(self):
        """A bounded interaction mesh; all detailed submaps remain on the PC."""
        points=[];faces=[];quality=[];count=0
        keys=[key for key in sorted(self.index) if self.index[key]['points'] and self.index[key]['faces']]
        budget=max(16,100000//max(1,len(keys)))
        for key in keys:
            model=self._model(key)
            if not model.faces:continue
            try:
                selected,triangles=model._surface(budget,fragments=False)
                value={'vertices':model.positions[selected].round(5).tolist(),'triangles':triangles.tolist(),
                       'quality':['reliable' if q==2 else 'uncertain' for q in model.quality()[selected]]}
            except SurfaceBudgetError:
                # An irreducible local boundary never deletes its detailed PC
                # map or interrupts live capture. Retain a complete part if it fits.
                try:value=model.mesh()
                except SurfaceBudgetError:continue
            if count+len(value['vertices'])>100000 or len(faces)+len(value['triangles'])>200000:continue
            points.extend(value['vertices']);quality.extend(value['quality'])
            faces.extend([[i+count for i in face] for face in value['triangles']]);count=len(points)
        if not faces:raise ValueError('No measured surfaces available yet')
        self.flush()
        return {'vertices':points,'triangles':faces,'quality':quality}
