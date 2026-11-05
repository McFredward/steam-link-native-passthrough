"""Directional observation and qualified-depth coverage; never geometry authority."""
import numpy as np


class DirectionCoverage:
    def __init__(self,initial=None):
        self.observed=np.zeros(72,np.uint8);self.measured=np.zeros(72,np.uint8)
        if initial is not None:
            for name in ('observed','measured'):
                values=initial.get(name)
                if type(values)is not list or len(values)!=72 or any(type(v)is not int or not 0<=v<=100 for v in values):
                    raise ValueError('Invalid retained direction coverage')
                setattr(self,name,np.asarray(values,dtype=np.uint8))

    def update(self,rays,support,valid):
        ray=rays[::4,::4]
        ray=ray/np.maximum(np.linalg.norm(ray,axis=-1)[...,None],1e-12)
        azimuth=np.mod(np.arctan2(ray[...,0],-ray[...,2])+np.pi,2*np.pi)
        elevation=np.arcsin(np.clip(ray[...,1],-1,1))
        index=np.minimum(11,(azimuth*12/(2*np.pi)).astype(int))+12*np.minimum(5,((elevation+np.pi/2)*6/np.pi).astype(int))
        count=np.bincount(index[support[::4,::4]],minlength=72)
        measured=np.bincount(index[valid[::4,::4]&support[::4,::4]],minlength=72)
        visible=count>=8
        self.observed[visible]=100
        fraction=np.rint(measured/np.maximum(count,1)*100).astype(np.uint8)
        self.measured[visible]=np.maximum(self.measured[visible],fraction[visible])
        return {'observed':self.observed.tolist(),'measured':self.measured.tolist()}
