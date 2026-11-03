"""Depth-initialized Gaussian surfels with revisit refinement and measured connectivity.

This is a geometric Gaussian map, not an optimized photometric radiance field.
Polygons are extracted from compatible observed neighbours only, preserving gaps.
"""
from __future__ import annotations

import base64
import numpy as np
from scipy.spatial import cKDTree
from scipy.ndimage import distance_transform_edt


class GaussianMap:
    def __init__(self, limit=60000, step=4):
        if not 1000 <= limit <= 80000 or step not in (4, 6, 8):
            raise ValueError("Invalid Gaussian map budget")
        self.limit, self.step = limit, step
        self.capacity = min(320000, limit*4)
        self.recycled = self.coarsenings = 0
        self.capacity_limited = False
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

    def integrate(self, points, normals, valid, depth, camera, focal, room=None):
        if room is not None and len(self.positions):
            local = (self.positions-room[:, 3]) @ room[:, :3]
            old_depth = -local[:, 2]
            height, width = depth.shape
            px = np.rint(local[:, 0]*focal/np.maximum(old_depth, .01)+(width-1)/2).astype(int)
            py = np.rint(-local[:, 1]*focal/np.maximum(old_depth, .01)+(height-1)/2).astype(int)
            inside = (old_depth > .25) & (px >= 0) & (px < width) & (py >= 0) & (py < height)
            selected = np.flatnonzero(inside)
            x, y = px[selected], py[selected]
            contradicted = valid[y, x] & (old_depth[selected] > depth[y, x]+.12)
            target = selected[contradicted]
            self.occluded[target] = np.minimum(self.occluded[target].astype(int)+1, 255)
            self.occluded[selected[~contradicted]] = 0
            self.active[self.occluded >= 3] = False
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
        if len(self.positions):
            active_ids = np.flatnonzero(self.active)
            if not len(active_ids):
                active_ids = np.arange(len(self.positions))
            distance, index = cKDTree(self.positions[active_ids]).query(measured, distance_upper_bound=.045)
            index = active_ids[np.minimum(index, len(active_ids)-1)]
            compatible = (distance < .045) & (np.sum(self.normals[index]*measured_n, axis=1) > .90)
            compatible &= np.abs(np.sum((self.positions[index]-measured)*measured_n, axis=1)) < .035
            match[compatible] = index[compatible]
            # One update per surfel per observation; a dense patch is not many independent views.
            _, first = np.unique(match[compatible], return_index=True)
            selected = np.flatnonzero(compatible)[first]
            target = match[selected]
            error = np.linalg.norm(self.positions[target]-measured[selected], axis=1)
            weight = np.minimum(self.weights[target], 15)
            alpha = 1/(weight+1)
            self.positions[target] += (measured[selected]-self.positions[target])*alpha[:, None]
            combined = self.normals[target]*(1-alpha[:, None])+measured_n[selected]*alpha[:, None]
            self.normals[target] = combined/np.maximum(np.linalg.norm(combined, axis=1)[:, None], 1e-12)
            self.sigma[target] += (sigma[selected]-self.sigma[target])*alpha
            self.weights[target] = weight+1
            self.active[target] = True
            self.occluded[target] = 0
            self.residuals[target] = self.residuals[target]*.85+error*.15
            different = np.linalg.norm(self.viewpoints[target]-camera, axis=1) >= .025
            self.views[target[different]] = np.minimum(self.views[target[different]].astype(int)+1, 255)
            self.viewpoints[target[different]] = camera
        new = np.flatnonzero(match < 0)
        # Consolidate newly observed overlapping samples before allocating stable IDs.
        if len(new):
            observed_new = new
            keys = np.floor(measured[new]/.018).astype(np.int32)
            _, unique, assignment = np.unique(keys, axis=0, return_index=True, return_inverse=True)
            representatives = new[unique]
            new = representatives
            free = np.flatnonzero(~self.active)
            if len(new) > len(free)+self.capacity-len(self.positions):
                self._coarsen()
                # Compaction can change the representatives matched above.
                # These assignments belong to the pre-compaction observation;
                # discard their faces, rather than joining unrelated surfaces.
                match[:] = -1
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
        from scipy.ndimage import minimum_filter
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
            for face in tri[compatible]:
                self.faces.add(tuple(sorted(map(int, face))))
        if len(self.faces)>640000:
            faces=np.asarray(sorted(self.faces),np.int32)
            keep=self._balanced(self.positions[faces].mean(axis=1),640000)
            self.faces={tuple(map(int,f)) for f in faces[keep]}
        self.frames += 1

    def quality(self):
        return np.where((self.views >= 3) & (self.residuals < .012), 2, 1)

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
        self.weights, self.views = np.ones(count, np.float32)*8, np.ones(count, np.uint8)
        self.views[ids] = np.where(values[:, 10] == 2, 3, 1)
        self.viewpoints = np.full((count, 3), 1000, np.float32)
        self.residuals = np.zeros(count, np.float32)
        self.active, self.occluded = np.zeros(count, bool), np.zeros(count, np.uint8)
        self.active[ids] = True
        self.capacity = max(self.capacity, count)
        self.faces = set()
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

    def _records(self,selected):
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
        records["values"][:, 10] = self.quality()[selected]
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

    def _surface(self,budget):
        active=np.flatnonzero(self.active)
        if not len(active):return active,np.empty((0,3),np.int32)
        selected=active;mapping=np.full(len(self.positions),-1,np.int32)
        inverse=np.arange(len(active))
        if len(active)>budget:
            for scale in (.025,.035,.05,.07,.10,.14):
                keys=np.column_stack((np.floor(self.positions[active]/scale).astype(np.int32),
                                      np.rint(self.normals[active]*2).astype(np.int32)))
                _,first,inverse=np.unique(keys,axis=0,return_index=True,return_inverse=True)
                selected=active[first]
                if len(selected)<=budget:break
            keep=self._balanced(self.positions[selected],budget)
            remap=np.full(len(selected),-1,np.int32);remap[keep]=np.arange(len(keep))
            selected=selected[keep];inverse=remap[inverse]
        mapping[active]=inverse
        if not self.faces:return selected,np.empty((0,3),np.int32)
        original=np.asarray(sorted(self.faces),np.int32)
        original=original[np.all(self.active[original],axis=1)]
        faces=mapping[original]
        good=np.all(faces>=0,axis=1)
        faces=np.sort(faces[good],axis=1)
        faces=np.unique(faces,axis=0)
        faces=faces[(faces[:,0]!=faces[:,1])&(faces[:,1]!=faces[:,2])]
        if not len(faces):return selected,faces
        p=self.positions[selected[faces]]
        area=np.linalg.norm(np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]),axis=1)
        edges=np.linalg.norm(p-np.roll(p,1,axis=1),axis=2)
        faces=faces[(area>1e-7)&(edges.max(axis=1)<.16)]
        # Reclustered faces retain measured support. Check their center and
        # edge midpoints against the original oriented Gaussian footprints.
        if len(active)>budget and len(faces):
            p=self.positions[selected[faces]]
            samples=np.concatenate((p.mean(axis=1)[:,None],(p+np.roll(p,1,axis=1))*.5),axis=1)
            tree=cKDTree(self.positions[active]);distance,index=tree.query(samples.reshape(-1,3),k=min(4,len(active)))
            if index.ndim==1:index=index[:,None]
            neighbours=active[index];delta=samples.reshape(-1,3)[:,None]-self.positions[neighbours]
            plane=np.sum(delta*self.normals[neighbours],axis=-1)
            tangent=np.maximum(0,np.sum(delta*delta,axis=-1)-plane*plane)
            covered=(np.abs(plane)<.012)&(tangent<=(self.sigma[neighbours]*3)**2)
            faces=faces[covered.any(axis=1).reshape(-1,4).all(axis=1)]
        if len(faces)>budget*2:
            faces=faces[self._balanced(self.positions[selected[faces]].mean(axis=1),budget*2)]
        return selected,faces

    def _coarsen(self):
        selected,faces=self._surface(max(1000,self.capacity*3//4))
        self.active[:]=False;self.active[selected]=True
        self.faces={tuple(sorted(map(int,f))) for f in selected[faces]}
        self.coarsenings+=1

    def snapshot(self):
        selected,faces=self._surface(self.limit)
        dtype=np.dtype([('id','<u4'),('point','<f4',(3,)),('quality','u1'),('pad','u1',(3,))])
        vertices=np.zeros(len(selected),dtype=dtype)
        vertices['id']=selected;vertices['point']=self.positions[selected];vertices['quality']=self.quality()[selected]
        return {'gaussians':self._records(selected),
                'surface_vertices':base64.b64encode(vertices.tobytes()).decode(),
                'surface_triangles':base64.b64encode(np.asarray(faces,dtype='<u4').tobytes()).decode(),
                'face_count':len(faces),'map_points':int(self.active.sum()),'map_capacity':self.capacity,
                'recycled_points':self.recycled,'coarsenings':self.coarsenings}

    def preview(self):
        return self._records(self._surface(self.limit)[0])

    def mesh(self):
        if not self.faces:
            raise ValueError("No measured surface connectivity. Capture more viewpoints first.")
        selected,faces = self._surface(min(100000,self.limit*2))
        if not len(faces):
            raise ValueError("No consistent measured surfaces. Continue scanning from another angle.")
        used, inverse = np.unique(faces, return_inverse=True);used=selected[used]
        return {"vertices": self.positions[used].round(5).tolist(), "triangles": inverse.reshape(-1, 3).tolist(),
                "quality": ["reliable" if q == 2 else "uncertain" for q in self.quality()[used]]}
