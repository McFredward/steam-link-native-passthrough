"""Validate PC Gaussian results before exposing them to the native MR renderer."""
from __future__ import annotations

import base64
import os
from pathlib import Path
import struct
import stat
import tempfile

from .capture import Reconstruction
from .geometry import Mesh, RoomError, require


class PcResultError(RoomError):
    """Fixed public reasons; never expose remote payload contents in a notice."""
    messages = {
        "binding": "PC preview belongs to another capture",
        "sequence": "PC preview is out of date",
        "model": "PC preview has an invalid format or size",
        "connectivity": "PC preview has invalid measured connectivity",
        "record": "PC preview has an invalid Gaussian record",
        "covariance": "PC preview has an invalid Gaussian shape",
    }

    def __init__(self, code):
        self.code = "pc-preview-"+code
        super().__init__(self.messages[code])


def checked(condition, code):
    if not condition:
        raise PcResultError(code)


class PcReconstruction(Reconstruction):
    def __init__(self, binding, root, *, lazy=False):
        super().__init__(binding)
        self.root = Path(root)
        self.path = self.surface_path = None
        self.final_mesh = False
        self.phase_ms = {}
        self.map_points = self.map_capacity = self.recycled_points = self.coarsenings = 0
        self.count = self.reliable = self.faces = 0
        self._mesh = Mesh()
        self.chunks={}
        self.preview_camera=None
        self.tile_count=0
        self._chunk_mesh_sequence=-1
        self.lazy=lazy
        self.coverage={'observed':[0]*72,'measured':[0]*72}

    def accept(self, result):
        checked(result["binding"] == {"session_id": self.binding.session_id, "origin_id": self.binding.origin_id,
                "calibration_id": self.binding.calibration_id}, "binding")
        sequence = result["sequence"]
        checked(type(sequence) is int and self.sequence < sequence < 2**63, "sequence")
        if result['event']=='pc_model' and 'chunks' in result:
            chunks=result['chunks']
            checked(type(chunks)is list and len(chunks)<=64,'model')
            removed=result.get('removed',[])
            checked(type(removed)is list and len(removed)<=8192 and all(type(k)is str and len(k)==32
                    and all(c in '0123456789abcdef' for c in k) for k in removed) and len(set(removed))==len(removed),'model')
            staged={}
            try:
                for chunk in chunks:
                    key=chunk.get('key')
                    checked(type(key)is str and len(key)==32 and all(c in '0123456789abcdef' for c in key)
                            and key not in staged and key not in removed,'model')
                    child=PcReconstruction(self.binding,self.root,lazy=True);staged[key]=child
                    child.accept({**chunk,'event':'pc_model','binding':result['binding'],'sequence':sequence})
                    cell=chunk.get('cell')
                    checked(type(cell)is list and len(cell)==3 and all(type(v)is int and abs(v)<=1001 for v in cell),'record')
                    child.cell=tuple(v+.5 for v in cell)
                    if 'center' in chunk:
                        center=chunk['center']
                        checked(type(center)is list and len(center)==3 and all(type(v)in(int,float) and abs(v)<=1000 for v in center),'record')
                        child.cell=tuple(center)
                camera=result.get('camera')
                checked(type(camera)is list and len(camera)==3 and all(type(v)in(int,float) and abs(v)<=1000 for v in camera),'record')
                stats={name:result.get(name,0) for name in ('map_points','map_capacity','recycled_points','coarsenings','tile_count')}
                checked(all(type(v)is int and 0<=v<2**40 for v in stats.values()),'record')
                phases=result.get('phase_ms',{})
                checked(type(phases)is dict and set(phases)<={'rectify','stereo','geometry','fusion','preview'}
                        and all(type(v)in(int,float) and 0<=v<=600000 for v in phases.values()),'record')
                coverage=result.get('coverage',self.coverage)
                checked(type(coverage)is dict and set(coverage)=={'observed','measured'}
                        and all(type(v)is list and len(v)==72 and all(type(n)is int and 0<=n<=100 for n in v) for v in coverage.values()),'record')
            except BaseException:
                for child in staged.values():child.close()
                raise
            old=[]
            for key,child in staged.items():
                if key in self.chunks:
                    previous=self.chunks[key]
                    # Keep each region's path stable. A new temporary path
                    # would replay its entire reveal on every observation.
                    for name in ('path','surface_path'):
                        current,target=getattr(child,name),getattr(previous,name)
                        if current is not None and target is not None:
                            os.replace(current,target);setattr(child,name,target);setattr(previous,name,None)
                    old.append(previous)
                self.chunks[key]=child
            for child in old:child.close()
            for key in removed:
                child=self.chunks.pop(key,None)
                if child is not None:child.close()
            self.preview_camera=tuple(camera);self.phase_ms=dict(phases)
            self.coverage={name:list(value) for name,value in coverage.items()}
            for name,value in stats.items():setattr(self,name,value)
            self.count=sum(child.count for child in self.chunks.values())
            self.faces=sum(child.faces for child in self.chunks.values())
            self.reliable=sum(child.reliable for child in self.chunks.values())
            self.sequence=sequence;self.accepted_frames+=1;self.final_mesh=False
            return
        if result["event"] == "pc_mesh":
            mesh = Mesh.from_dict(result["mesh"])
            require(bool(mesh.triangles), "No measured polygon surfaces")
        else:
            checked(result["event"] == "pc_model" and type(result["gaussians"]) is str
                    and len(result["gaussians"]) <= 5120000, "model")
            raw = base64.b64decode(result["gaussians"], validate=True)
            checked(0 < len(raw) <= 80000*48 and len(raw) % 48 == 0, "model")
            faces = result["face_count"]
            checked(type(faces) is int and 0 <= faces <= 160000, "connectivity")
            seen, reliable, gaussian_points = set(), 0, {}
            minimum, maximum = (.001*(1-1e-5))**2, (.15*(1+1e-5))**2
            for record in struct.iter_unpack("<I11f", raw):
                index, *v = record
                checked(index < 320000 and index not in seen and all(abs(x) <= 1000 for x in v)
                        and all(x == x for x in v), "record")
                seen.add(index)
                u, w = sum(x*x for x in v[3:6]), sum(x*x for x in v[6:9])
                # Unit tangent axes multiplied by the 1 mm floor are encoded
                # as float32. Rounding can put their squared length a few ulps
                # below 1e-6; that is still the same valid measured surface.
                checked(minimum <= u <= maximum and minimum <= w <= maximum
                        and abs(sum(a*b for a, b in zip(v[3:6], v[6:9]))) < (u*w)**.5*.02
                        and 0 < v[9] <= 1 and v[10] in (1, 2), "covariance")
                reliable += v[10] == 2
                gaussian_points[index]=(tuple(v[:3]),int(v[10]))
            surface = None
            if 'surface_vertices' in result:
                vertex_raw=base64.b64decode(result['surface_vertices'],validate=True)
                face_raw=base64.b64decode(result['surface_triangles'],validate=True)
                checked(len(vertex_raw)%20==0 and len(vertex_raw)<=100000*20
                        and len(face_raw)%12==0 and len(face_raw)<=200000*12,'connectivity')
                records=list(struct.iter_unpack('<I3fB3x',vertex_raw))
                checked(all(r[0]<320000 and r[4] in (0,1,2) for r in records)
                        and len({r[0] for r in records})==len(records),'connectivity')
                checked(len(records)==len(seen) and all(gaussian_points.get(r[0])==(tuple(r[1:4]),r[4]) for r in records),'connectivity')
                mesh=Mesh(tuple(tuple(r[1:4]) for r in records),tuple(struct.iter_unpack('<3I',face_raw)),
                          tuple(('building','uncertain','reliable')[r[4]] for r in records))
                surface=b'SFNPMR2\0'+struct.pack('<II',len(records),len(mesh.triangles))+vertex_raw+face_raw
                checked(len(mesh.triangles)==faces,'connectivity')
            stats={name:result.get(name,0) for name in ('map_points','map_capacity','recycled_points','coarsenings')}
            phases=result.get('phase_ms',{})
            checked(type(phases)is dict and set(phases)<={'rectify','stereo','geometry','fusion','preview'}
                    and all(type(v)in(int,float) and 0<=v<=600000 for v in phases.values()),'record')
            checked(all(type(v)is int and 0<=v<2**40 for v in stats.values()),'record')
            fd, name = tempfile.mkstemp(prefix=".pc-gaussians-", dir=self.root)
            try:
                with os.fdopen(fd, "wb") as stream:
                    stream.write(raw)
                if self.path:
                    os.replace(name, self.path)
                else:
                    self.path = Path(name)
            except BaseException:
                Path(name).unlink(missing_ok=True)
                raise
            self.count, self.reliable = len(seen), reliable
            if surface is not None:self._surface(surface)
            self._mesh = mesh if surface is not None and not self.lazy else Mesh()
            self.final_mesh = False
            self.faces = faces
            for name,value in stats.items():setattr(self,name,value)
            self.phase_ms=dict(phases)
        if result['event']=='pc_mesh':
            self.retain_aligned_mesh(mesh)
            self.final_mesh=True
        self.sequence = sequence
        self.accepted_frames += 1

    def mesh(self):
        if self.lazy and self.surface_path is not None:
            fd=os.open(self.surface_path,os.O_RDONLY|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_NONBLOCK',0))
            with os.fdopen(fd,'rb') as stream:
                info=os.fstat(stream.fileno())
                require(stat.S_ISREG(info.st_mode) and info.st_nlink==1 and 16<=info.st_size<=4400016
                        and (not hasattr(os,'getuid') or info.st_uid==os.getuid() and not info.st_mode&0o077),
                        'Invalid private surface extent')
                raw=stream.read(4400017)
            require(raw[:8]==b'SFNPMR2\0' and len(raw)==info.st_size,'Private surface changed')
            vertices,faces=struct.unpack_from('<II',raw,8)
            require(len(raw)==16+vertices*20+faces*12,'Invalid private surface records')
            records=list(struct.iter_unpack('<I3fB3x',raw[16:16+vertices*20]))
            require(all(r[4]in(0,1,2) for r in records),'Invalid private surface quality')
            return Mesh(tuple(tuple(r[1:4]) for r in records),tuple(struct.iter_unpack('<3I',raw[16+vertices*20:])),
                        tuple(('building','uncertain','reliable')[r[4]] for r in records))
        if self.chunks and not self.final_mesh and self._chunk_mesh_sequence!=self.sequence:
            # Interaction/checkpoint mesh has its own bound; all detailed live
            # chunks remain available to the renderer and on the paired PC.
            vertices=[];faces=[];quality=[]
            for child in self.chunks.values():
                mesh=child.mesh()
                if len(vertices)+len(mesh.vertices)>100000 or len(faces)+len(mesh.triangles)>200000:continue
                offset=len(vertices);vertices.extend(mesh.vertices);quality.extend(mesh.quality)
                faces.extend(tuple(i+offset for i in face) for face in mesh.triangles)
            self._mesh=Mesh(tuple(vertices),tuple(faces),tuple(quality))
            self._chunk_mesh_sequence=self.sequence
        return self._mesh

    def surface_files(self):
        if self.final_mesh:return []
        return [str(child.surface_path) for child in self.chunks.values() if child.surface_path is not None]

    def surface_tiles(self):
        if self.final_mesh:return []
        return [{'path':str(child.surface_path),'center':list(child.cell),'faces':child.faces}
                for child in self.chunks.values() if child.surface_path is not None]

    def has_surface(self):
        return bool(self.surface_path is not None or self.chunks)

    def update_coverage(self,result):
        checked(result.get('binding')=={name:getattr(self.binding,name) for name in ('session_id','origin_id','calibration_id')},'binding')
        coverage=result.get('coverage')
        if coverage is None:return
        checked(type(coverage)is dict and set(coverage)=={'observed','measured'}
                and all(type(v)is list and len(v)==72 and all(type(n)is int and 0<=n<=100 for n in v) for v in coverage.values()),'record')
        self.coverage={name:list(value) for name,value in coverage.items()}

    def _surface(self,raw):
        fd,name=tempfile.mkstemp(prefix='.pc-surfaces-',dir=self.root)
        try:
            with os.fdopen(fd,'wb') as stream:stream.write(raw)
            if self.surface_path:os.replace(name,self.surface_path)
            else:self.surface_path=Path(name)
        except BaseException:
            Path(name).unlink(missing_ok=True)
            raise

    def status(self):
        return {"accepted_frames": self.accepted_frames, "reliable_cells": self.reliable,
                "uncertain_cells": self.count-self.reliable, "gaussians": self.count,
                "phase_ms":dict(self.phase_ms), "measured_faces": self.faces, "backend": "pc-neural-gaussian",'final_mesh':self.final_mesh,
                **{name:getattr(self,name) for name in ('map_points','map_capacity','recycled_points','coarsenings')}}

    def retain_aligned_mesh(self, mesh):
        vertices=b''.join(struct.pack('<I3fB3x',i,*p,('building','uncertain','reliable').index(mesh.quality[i]))
                          for i,p in enumerate(mesh.vertices))
        faces=b''.join(struct.pack('<3I',*t) for t in mesh.triangles)
        self._surface(b'SFNPMR2\0'+struct.pack('<II',len(mesh.vertices),len(mesh.triangles))+vertices+faces)
        self._mesh = mesh
        self.faces = len(mesh.triangles)

    def close(self):
        for child in self.chunks.values():child.close()
        self.chunks.clear()
        if self.surface_path:
            self.surface_path.unlink(missing_ok=True);self.surface_path=None
        if self.path:
            self.path.unlink(missing_ok=True)
            self.path = None
