"""Depth-initialized Gaussian surfels with revisit refinement and measured connectivity.

This is a geometric Gaussian map, not an optimized photometric radiance field.
Polygons are extracted from compatible observed neighbours only, preserving gaps.
"""
from __future__ import annotations

import base64
import numpy as np
from scipy.spatial import cKDTree
from scipy.ndimage import distance_transform_edt, minimum_filter


class SurfaceBudgetError(ValueError):
    """A measured boundary cannot fit; retain the last complete preview."""


class GaussianMap:
    def __init__(self, limit=60000, step=4, *, cell_m=.018, min_tangent=.022, compact=True):
        if not 1000 <= limit <= 80000 or step not in (2, 4, 6, 8) or not .002<=cell_m<=.018:
            raise ValueError("Invalid Gaussian map budget")
        self.limit, self.step = limit, step
        self.cell_m,self.min_tangent,self.compact=cell_m,min_tangent,compact
        self.capacity = min(320000, limit*4)
        self.recycled = self.coarsenings = 0
        self.capacity_limited = False
        self.coarsening_refused_at = -1000
        self.positions = np.empty((0, 3), np.float32)
        self.normals = np.empty((0, 3), np.float32)
        self.sigma = np.empty(0, np.float32)
        self.weights = np.empty(0, np.float32)
        self.views = np.empty(0, np.uint8)
        self.viewpoints = np.empty((0, 3), np.float32)
        self.residuals = np.empty(0, np.float32)
        self.active = np.empty(0, bool)
        self.occluded = np.empty(0, np.uint8)
        self.faces = set()
        self.frames = 0
        self.seeded_orientation = False
        self.previous_snapshot = None

    def observe_visibility(self,depth,valid,room,focal,principal=None,minimum_depth=None):
        """Retire repeatable free-space conflicts even outside the new surface tile."""
        before=int(self.active.sum())
        if room is not None and len(self.positions):
            observed=np.flatnonzero(self.active)
            local = (self.positions[observed]-room[:, 3]) @ room[:, :3]
            old_depth = -local[:, 2]
            height, width = depth.shape
            cx,cy=principal or ((width-1)/2,(height-1)/2)
            px = np.rint(local[:, 0]*focal/np.maximum(old_depth, .01)+cx).astype(int)
            py = np.rint(-local[:, 1]*focal/np.maximum(old_depth, .01)+cy).astype(int)
            inside = (old_depth > .25) & (px >= 0) & (px < width) & (py >= 0) & (py < height)
            projected = np.flatnonzero(inside);selected=observed[projected]
            x,y=px[projected],py[projected]
            near=minimum_depth if minimum_depth is not None else minimum_filter(np.where(valid,depth,0),size=3,mode='constant')
            contradicted=valid[y,x]&(old_depth[projected]+.12<near[y,x])
            target=selected[contradicted]
            self.occluded[target]=np.minimum(self.occluded[target].astype(int)+1,255)
            self.occluded[selected[~contradicted]]=0
            self.active[self.occluded>=3]=False
        return before-int(self.active.sum())

    def integrate(self, points, normals, valid, depth, camera, focal, room=None, principal=None, visibility=None,minimum_depth=None):
        self.capacity_limited = False
        # Compact before computing this observation's correspondences. A
        # compaction during allocation invalidates both matches and new IDs.
        if self.compact and (int(self.active.sum()) > self.limit*2 or len(self.faces) > self.limit*3
                or int(self.active.sum())+int(valid[::self.step, ::self.step].sum()) > self.capacity):
            self._coarsen()
        if self.seeded_orientation:
            flip=np.sum(self.normals*(camera-self.positions),axis=1)<0
            self.normals[flip]*=-1
            self.seeded_orientation=False
        self.observe_visibility(depth,valid if visibility is None else visibility,room,focal,principal,minimum_depth)
        step = self.step
        p, n = points[::step, ::step], normals[::step, ::step]
        good = valid[::step, ::step].copy()
        z = depth[::step, ::step]
        ids = np.full(good.shape, -1, np.int32)
        flat = np.flatnonzero(good)
        measured, measured_n = p.reshape(-1, 3)[flat], n.reshape(-1, 3)[flat]
        if not len(flat):
            return
        sigma = np.clip(z.ravel()[flat]*step/focal*.48, .002, .045)
        # A Gaussian's visible support must not spill into an unmeasured image hole.
        support_radius = distance_transform_edt(valid)[::step, ::step].ravel()[flat]
        sigma = np.maximum(.001, np.minimum(sigma, z.ravel()[flat]/focal*support_radius/3))
        match = np.full(len(flat), -1, np.int32)
        active_ids = np.flatnonzero(self.active)
        if len(active_ids):
            distance, index = cKDTree(self.positions[active_ids]).query(measured, k=min(4,len(active_ids)), distance_upper_bound=.07)
            if distance.ndim==1:distance,index=distance[:,None],index[:,None]
            index = active_ids[np.minimum(index, len(active_ids)-1)]
            delta = measured[:,None]-self.positions[index]
            plane = np.sum(delta*self.normals[index],axis=-1)
            tangent = np.sqrt(np.maximum(0,np.sum(delta*delta,axis=-1)-plane*plane))
            compatible = (distance < .07) & (np.sum(self.normals[index]*measured_n[:,None], axis=-1) > .75)
            compatible &= (np.abs(plane)<.035)&(tangent<np.maximum(self.min_tangent,self.sigma[index]*1.5))
            score=np.where(compatible,distance+np.abs(plane),np.inf)
            best=np.argmin(score,axis=1);chosen=np.arange(len(measured));usable=np.isfinite(score[chosen,best])
            match[usable]=index[chosen[usable],best[usable]]
            # Aggregate all pixels assigned to a cell, once per observation.
            # Taking the first pixel biases each revisit toward an image edge
            # and gradually drags the map away from its original coverage.
            selected=np.flatnonzero(usable);target,inverse=np.unique(match[selected],return_inverse=True)
            counts=np.bincount(inverse)
            average=lambda values:np.column_stack([np.bincount(inverse,weights=values[:,i])/counts for i in range(values.shape[1])])
            mean,mean_n=average(measured[selected]),average(measured_n[selected])
            mean_n/=np.maximum(np.linalg.norm(mean_n,axis=1)[:,None],1e-12)
            error=np.abs(np.sum((mean-self.positions[target])*self.normals[target],axis=1))
            weight=np.minimum(self.weights[target],15);alpha=1/(weight+1)
            # Keep tangential coverage stable; refine depth along its measured
            # normal instead of collapsing neighbouring cells onto one another.
            shift=np.sum((mean-self.positions[target])*self.normals[target],axis=1)
            self.positions[target]+=self.normals[target]*(shift*alpha)[:,None]
            combined=self.normals[target]*(1-alpha[:,None])+mean_n*alpha[:,None]
            self.normals[target]=combined/np.maximum(np.linalg.norm(combined,axis=1)[:,None],1e-12)
            footprint=np.bincount(inverse,weights=sigma[selected])/counts
            self.sigma[target]=np.maximum(self.sigma[target],footprint)
            self.weights[target]=weight+1;self.active[target]=True;self.occluded[target]=0
            self.residuals[target]=self.residuals[target]*.85+error*.15
            different=np.linalg.norm(self.viewpoints[target]-camera,axis=1)>=.015
            self.views[target[different]]=np.minimum(self.views[target[different]].astype(int)+1,255)
            self.viewpoints[target[different]]=camera
        new = np.flatnonzero(match < 0)
        # Consolidate newly observed overlapping samples before allocating stable IDs.
        if len(new):
            observed_new = new
            keys = np.floor(measured[new]/self.cell_m).astype(np.int32)
            _, unique, assignment = np.unique(keys, axis=0, return_index=True, return_inverse=True)
            representatives = new[unique]
            new = representatives
            free = np.flatnonzero(~self.active)
            take = min(len(new), len(free))
            reused = free[:take]
            if take:
                if self.faces:
                    faces=np.asarray(sorted(self.faces),np.int32)
                    retired=np.zeros(len(self.positions),bool);retired[reused]=True
                    self.faces={tuple(map(int,f)) for f in faces[~retired[faces].any(axis=1)]}
                source = new[:take]
                self.positions[reused], self.normals[reused] = measured[source], measured_n[source]
                self.sigma[reused], self.weights[reused] = sigma[source], 1
                self.views[reused], self.viewpoints[reused] = 1, camera
                self.residuals[reused], self.occluded[reused], self.active[reused] = 0, 0, True
                match[source] = reused
                self.recycled += take
            new = new[take:]
            self.capacity_limited = len(new) > self.capacity-len(self.positions)
            new = new[:max(0,self.capacity-len(self.positions))]
            start = len(self.positions)
            match[new] = np.arange(start, start+len(new))
            self.positions = np.concatenate((self.positions, measured[new].astype(np.float32)))
            self.normals = np.concatenate((self.normals, measured_n[new].astype(np.float32)))
            self.sigma = np.concatenate((self.sigma, sigma[new].astype(np.float32)))
            self.weights = np.concatenate((self.weights, np.ones(len(new), np.float32)))
            self.views = np.concatenate((self.views, np.ones(len(new), np.uint8)))
            self.viewpoints = np.concatenate((self.viewpoints, np.broadcast_to(camera, (len(new), 3)).astype(np.float32)))
            self.residuals = np.concatenate((self.residuals, np.zeros(len(new), np.float32)))
            self.active = np.concatenate((self.active, np.ones(len(new), bool)))
            self.occluded = np.concatenate((self.occluded, np.zeros(len(new), np.uint8)))
            # Every measured pixel in an allocated cell shares its real
            # representative. Dropping these assignments loses connectivity
            # on nearby surfaces whose samples are closer than the cell size.
            match[observed_new] = match[representatives][assignment]
        ids.ravel()[flat] = match
        a, b, c, d = ids[:-1, :-1], ids[:-1, 1:], ids[1:, :-1], ids[1:, 1:]
        # Every intervening pixel must be observed. Four measured corners do
        # not authorize a triangle over a small unseen hole inside their cell.
        supported = minimum_filter(valid.astype(np.uint8),size=step+1,mode='constant',cval=0)
        supported = supported[step//2::step,step//2::step][:a.shape[0],:a.shape[1]].ravel().astype(bool)
        for tri in (np.stack((a, b, c), -1).reshape(-1, 3), np.stack((b, d, c), -1).reshape(-1, 3)):
            tri = tri[supported]
            tri = tri[np.all(tri >= 0, axis=1)]
            if not len(tri):
                continue
            vertices = self.positions[tri]
            edges = np.linalg.norm(vertices-np.roll(vertices, 1, axis=1), axis=2)
            limit = np.maximum(.045, np.max(self.sigma[tri], axis=1)*5)
            compatible = np.max(edges, axis=1) < np.minimum(.16, limit)
            compatible &= np.linalg.norm(np.cross(vertices[:, 1]-vertices[:, 0], vertices[:, 2]-vertices[:, 0]), axis=1) > 1e-7
            incoming={tuple(face) for face in np.sort(tri[compatible],axis=1).tolist()}
            additions=incoming-self.faces
            remaining=max(0,640000-len(self.faces))
            if len(additions)>remaining:
                self.faces.update(sorted(additions)[:remaining]);self.capacity_limited=True
            else:self.faces.update(additions)
        self.frames += 1

    def quality(self):
        if self.min_tangent<.01:
            return np.where((self.views>=2)&(self.weights>=3)&(self.residuals<.008),2,1)
        return np.where(((self.views >= 3) | (self.weights >= 6)) & (self.residuals < .008), 2, 1)

    def restore(self, encoded):
        raw = base64.b64decode(encoded, validate=True)
        if not raw or len(raw) > 80000*48 or len(raw) % 48:
            raise ValueError("Invalid retained Gaussian map")
        records = np.frombuffer(raw, dtype=np.dtype([("id", "<u4"), ("values", "<f4", (11,))]))
        ids, values = records["id"], records["values"]
        if ids.max() >= 320000 or len(np.unique(ids)) != len(ids) or not np.isfinite(values).all():
            raise ValueError("Invalid retained Gaussian records")
        count = int(ids.max())+1
        self.positions, self.normals = np.zeros((count, 3), np.float32), np.zeros((count, 3), np.float32)
        normal = np.cross(values[:, 3:6], values[:, 6:9])
        self.positions[ids] = values[:, :3]
        self.normals[ids] = normal/np.maximum(np.linalg.norm(normal, axis=1)[:, None], 1e-12)
        self.sigma = np.zeros(count, np.float32)
        self.sigma[ids] = np.linalg.norm(values[:, 3:6], axis=1)
        self.weights, self.views = np.ones(count, np.float32), np.ones(count, np.uint8)
        self.weights[ids]=np.where(values[:,10]==2,8,1)
        self.views[ids] = np.where(values[:, 10] == 2, 3, 1)
        self.viewpoints = np.full((count, 3), 1000, np.float32)
        self.residuals = np.zeros(count, np.float32)
        self.active, self.occluded = np.zeros(count, bool), np.zeros(count, np.uint8)
        self.active[ids] = True
        self.capacity = max(self.capacity, count)
        self.faces = set()
        self.previous_snapshot = None
        # Rebuild only overlapping local Gaussian support, never a room hull.
        distances, neighbours = cKDTree(values[:, :3]).query(values[:, :3], k=min(7, len(ids)))
        if distances.ndim == 2:
            for a, (distance, adjacent) in enumerate(zip(distances, neighbours)):
                for b, c in zip(adjacent[1:-1], adjacent[2:]):
                    tri = ids[[a, b, c]]
                    p, n = self.positions[tri], self.normals[tri]
                    if (len(set(map(int, tri))) == 3 and np.max(np.linalg.norm(p-np.roll(p, 1, axis=0), axis=1)) < min(.16, float(self.sigma[tri].max())*4)
                            and np.min(n @ n.T) > .98 and np.linalg.norm(np.cross(p[1]-p[0], p[2]-p[0])) > 1e-7):
                        self.faces.add(tuple(sorted(map(int, tri))))

    def restore_surface(self, raw):
        """Retain measured connectivity, including reduced large planar faces."""
        if len(raw)<16 or raw[:8]!=b'SFNPMR2\0':raise ValueError('Invalid retained surface')
        count,faces=np.frombuffer(raw[8:16],dtype='<u4')
        if not 0<count<=100000 or not 0<faces<=200000 or len(raw)!=16+int(count)*20+int(faces)*12:
            raise ValueError('Invalid retained surface extent')
        records=np.frombuffer(raw,dtype=np.dtype([('id','<u4'),('p','<f4',(3,)),('q','u1'),('pad','u1',(3,))]),count=int(count),offset=16)
        ids,points,quality=records['id'],records['p'],records['q']
        triangles=np.frombuffer(raw,dtype='<u4',count=int(faces)*3,offset=16+int(count)*20).reshape(-1,3)
        if (ids.max()>=320000 or len(np.unique(ids))!=len(ids) or not np.isfinite(points).all()
                or (abs(points)>1000).any() or (quality>2).any() or triangles.max()>=count):
            raise ValueError('Invalid retained surface records')
        p=points[triangles];area=np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]);size=np.linalg.norm(area,axis=1)
        if (size<=1e-7).any():raise ValueError('Invalid retained surface connectivity')
        first=np.full(int(count),len(triangles),np.int32)
        for j in range(3):np.minimum.at(first,triangles[:,j],np.arange(len(triangles)))
        used=np.flatnonzero(first<len(triangles));reference=np.zeros((int(count),3),np.float32);reference[used]=area[first[used]]
        normal=np.zeros_like(reference);radius=np.full(int(count),.045,np.float32)
        for j in range(3):
            target=triangles[:,j];sign=np.where(np.sum(area*reference[target],axis=1)<0,-1,1)
            np.add.at(normal,target,area*sign[:,None])
            np.minimum.at(radius,target,np.linalg.norm(p[:,j]-p[:,(j+1)%3],axis=1)/3)
        normal/=np.maximum(np.linalg.norm(normal,axis=1),1e-12)[:,None]
        count=int(ids.max())+1
        self.positions,self.normals=np.zeros((count,3),np.float32),np.zeros((count,3),np.float32)
        self.positions[ids],self.normals[ids]=points,normal
        self.sigma=np.zeros(count,np.float32);self.sigma[ids]=np.clip(radius,.001,.045)
        self.weights=np.ones(count,np.float32);self.weights[ids]=np.where(quality==2,8,1)
        self.views=np.ones(count,np.uint8);self.views[ids]=np.where(quality==2,3,1)
        self.viewpoints=np.full((count,3),1000,np.float32);self.residuals=np.zeros(count,np.float32)
        self.active=np.zeros(count,bool);self.active[ids[used]]=True;self.occluded=np.zeros(count,np.uint8)
        self.faces={tuple(sorted(map(int,f))) for f in ids[triangles]}
        self.capacity=max(self.capacity,count);self.seeded_orientation=True
        self.previous_snapshot=None

    def _records(self,selected,quality=None):
        count = len(selected)
        normals = self.normals[selected]
        guide = np.tile((0., 1., 0.), (count, 1))
        guide[np.abs(normals[:, 1]) > .9] = (1., 0., 0.)
        u = np.cross(normals, guide)
        u /= np.maximum(np.linalg.norm(u, axis=1)[:, None], 1e-12)
        v = np.cross(normals, u)
        records = np.zeros(count, dtype=np.dtype([("id", "<u4"), ("values", "<f4", (11,))]))
        records["id"] = selected
        records["values"][:, :3] = self.positions[selected]
        records["values"][:, 3:6] = u*self.sigma[selected, None]
        records["values"][:, 6:9] = v*self.sigma[selected, None]
        records["values"][:, 9] = .60
        records["values"][:, 10] = self.quality()[selected] if quality is None else quality
        return base64.b64encode(records.tobytes()).decode()

    @staticmethod
    def _balanced(points,budget):
        """Round-robin spatial cells so early dense views cannot monopolize a preview."""
        if len(points)<=budget:return np.arange(len(points))
        keys=np.floor(points/.4).astype(np.int32)
        _,tile=np.unique(keys,axis=0,return_inverse=True)
        order=np.argsort(tile,kind='stable');sorted_tile=tile[order]
        starts=np.r_[0,np.flatnonzero(np.diff(sorted_tile))+1]
        ranks=np.arange(len(order))-np.repeat(starts,np.diff(np.r_[starts,len(order)]))
        return order[np.lexsort((sorted_tile,ranks))[:budget]]

    def _surface(self,budget,*,fragments=True):
        active=np.flatnonzero(self.active)
        empty=np.empty((0,3),np.int32)
        if not len(active):return active,empty
        if not self.faces:return active[self._balanced(self.positions[active],budget)],empty
        original=np.asarray(sorted(self.faces),np.int32)
        original=original[np.all(self.active[original],axis=1)]
        if not len(original):return active[self._balanced(self.positions[active],budget)],empty
        p=self.positions[original]
        area=np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0])
        # Input neighbours were already bounded when observed. A subsequent
        # boundary-preserving reduction can legitimately contain longer faces.
        good=np.linalg.norm(area,axis=1)>1e-7
        original=original[good];area=area[good]
        if not len(original):return active[self._balanced(self.positions[active],budget)],empty
        # Preserve consistent winding before reducing a measured mesh. Sorting
        # stable IDs alone creates arbitrarily flipped adjacent triangles.
        flip=np.sum(area*self.normals[original].mean(axis=1),axis=1)<0
        original[flip]=original[flip][:, (0,2,1)]
        selected,inverse=np.unique(original,return_inverse=True)
        faces=inverse.reshape(-1,3)
        if len(selected)>budget or len(faces)>budget*2:
            if fragments and self._coarsen():
                return self._surface(budget,fragments=False)
            import pyfqmr
            reducer=pyfqmr.Simplify();reducer.setMesh(self.positions[selected].astype(np.float64),faces)
            reducer.simplify_mesh(target_count=budget,max_iterations=40,aggressiveness=5,
                                  preserve_border=True,verbose=False)
            reduced,triangles,_=reducer.getMesh()
            # Keep the preview on real Gaussian samples. Boundary vertices are
            # locked by the reducer; snapping only interiors avoids inventing
            # a new hull or erasing measured openings during voxel clustering.
            distance,nearest=cKDTree(self.positions[selected]).query(reduced)
            identities=selected[nearest]
            valid=np.all(distance[triangles]<np.maximum(.025,self.sigma[identities[triangles]]*3),axis=1)
            source=identities[triangles[valid]]
            if len(source):selected,inverse=np.unique(source,return_inverse=True);faces=inverse.reshape(-1,3)
            faces=faces[(faces[:,0]!=faces[:,1])&(faces[:,1]!=faces[:,2])&(faces[:,2]!=faces[:,0])]
            p=self.positions[selected[faces]]
            faces=faces[np.linalg.norm(np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]),axis=1)>1e-7]
        if not len(faces):
            selected,inverse=np.unique(original,return_inverse=True);faces=inverse.reshape(-1,3)
        used,inverse=np.unique(faces,return_inverse=True)
        if len(used)>budget or len(faces)>budget*2:
            raise SurfaceBudgetError('Measured boundaries exceed the preview budget')
        return selected[used],inverse.reshape(-1,3)

    def _coarsen(self):
        from .live_surface import merge_measured
        if not self.compact:return False
        if self.frames-self.coarsening_refused_at<5 or not self.faces:return False
        faces=np.asarray(sorted(self.faces),np.int32)
        faces=faces[self.active[faces].all(axis=1)]
        if not len(faces):return False
        points,normals,quality,faces=merge_measured(self.positions,self.normals,self.quality(),faces)
        count=len(points)
        if not len(faces) or count>=int(self.active.sum()):
            self.coarsening_refused_at=self.frames
            return False
        active=np.flatnonzero(self.active)
        _,nearest=cKDTree(self.positions[active]).query(points)
        nearest=active[nearest]
        flip=np.sum(normals*self.normals[nearest],axis=1)<0
        normals[flip]*=-1
        sigma=self.sigma[nearest].copy()
        viewpoints=self.viewpoints[nearest].copy()
        # Publish the complete measured union atomically. Old unreferenced IDs
        # become reusable; no observation can keep pre-compaction matches.
        self.positions[:count],self.normals[:count]=points,normals
        self.sigma[:count]=sigma
        self.weights[:count]=np.where(quality==2,8,1)
        self.views[:count]=np.where(quality==2,3,1)
        self.viewpoints[:count]=viewpoints;self.residuals[:count]=0
        self.occluded[:]=0;self.active[:]=False;self.active[:count]=True
        self.faces={tuple(sorted(map(int,f))) for f in faces}
        self.coarsenings+=1
        return True

    def snapshot(self):
        try:
            selected,faces=self._surface(self.limit)
            dtype=np.dtype([('id','<u4'),('point','<f4',(3,)),('quality','u1'),('pad','u1',(3,))])
            vertices=np.zeros(len(selected),dtype=dtype)
            vertices['id']=selected;vertices['point']=self.positions[selected];vertices['quality']=self.quality()[selected]
            self.previous_snapshot={'gaussians':self._records(selected),
                    'surface_vertices':base64.b64encode(vertices.tobytes()).decode(),
                    'surface_triangles':base64.b64encode(np.asarray(faces,dtype='<u4').tobytes()).decode(),
                    'face_count':len(faces)}
        except SurfaceBudgetError:
            self.capacity_limited=True
            if self.previous_snapshot is None:raise
        return {**self.previous_snapshot,'map_points':int(self.active.sum()),'map_capacity':self.capacity,
                'recycled_points':self.recycled,'coarsenings':self.coarsenings}

    def preview(self):
        return self.snapshot()['gaussians']

    def mesh(self):
        if not self.faces:
            raise ValueError("No measured surface connectivity. Capture more viewpoints first.")
        try:selected,faces = self._surface(min(100000,self.limit*2))
        except SurfaceBudgetError:
            if self.previous_snapshot is None:raise
            raw=base64.b64decode(self.previous_snapshot['surface_vertices'])
            vertices=np.frombuffer(raw,dtype=np.dtype([('id','<u4'),('point','<f4',(3,)),('quality','u1'),('pad','u1',(3,))]))
            faces=np.frombuffer(base64.b64decode(self.previous_snapshot['surface_triangles']),dtype='<u4').reshape(-1,3)
            return {'vertices':vertices['point'].round(5).tolist(),'triangles':faces.tolist(),
                    'quality':['reliable' if q==2 else 'uncertain' for q in vertices['quality']]}
        if not len(faces):
            raise ValueError("No consistent measured surfaces. Continue scanning from another angle.")
        used, inverse = np.unique(faces, return_inverse=True);used=selected[used]
        return {"vertices": self.positions[used].round(5).tolist(), "triangles": inverse.reshape(-1, 3).tolist(),
                "quality": ["reliable" if q == 2 else "uncertain" for q in self.quality()[used]]}
