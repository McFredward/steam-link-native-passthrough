"""Subpixel photometric verification around neural disparity proposals."""
import numpy as np
from scipy.ndimage import uniform_filter,minimum_filter


def refine_one(left,right,disparity,left_mask,right_mask):
    left=left.astype(np.float32);right=right.astype(np.float32)
    def normalized(image):
        mean=uniform_filter(image,5,mode='nearest')
        std=np.sqrt(np.maximum(0,uniform_filter(image*image,5,mode='nearest')-mean*mean))
        return (image-mean)/np.maximum(std,2),std
    lz,texture=normalized(left);rz,right_texture=normalized(right)
    h,w=left.shape;y,x=np.mgrid[:h,:w]
    costs=[];supports=[]
    for offset in (-2.,-1.,0.,1.,2.):
        coordinate=x-disparity-offset
        lo=np.clip(np.floor(coordinate).astype(np.int32),0,w-2);t=np.clip(coordinate-lo,0,1)
        sample=rz[y,lo]*(1-t)+rz[y,lo+1]*t
        support=left_mask&right_mask[y,lo]&right_mask[y,lo+1]&(coordinate>=0)&(coordinate<w-1)
        support&=(texture>=2.5)&(right_texture[y,lo]>=2.5)
        support=minimum_filter(support.astype(np.uint8),5,mode='constant').astype(bool)
        costs.append(np.where(support,uniform_filter((lz-sample)**2,5,mode='nearest'),np.inf))
        supports.append(support)
    costs=np.asarray(costs);best=np.argmin(costs,axis=0)
    a=costs[np.maximum(0,best-1),y,x];b=costs[best,y,x];c=costs[np.minimum(4,best+1),y,x]
    finite=np.isfinite(a)&np.isfinite(b)&np.isfinite(c)
    aa,bb,cc=(np.where(finite,v,0) for v in (a,b,c))
    curvature=aa-2*bb+cc
    shift=np.zeros_like(disparity,dtype=np.float32)
    np.divide(.5*(aa-cc),curvature,out=shift,where=finite&(curvature>1e-5))
    correction=best.astype(np.float32)-2+np.clip(shift,-.5,.5)
    refined=finite&(b<.65)&(curvature>.025)&(best>0)&(best<4)
    # A sharp minimum is required to move a proposal, not to accept an
    # already photometrically supported bilateral neural measurement. Broad
    # real-world texture often has a flat local disparity cost curve.
    supported=np.isfinite(costs[2])&(costs[2]<.65)
    valid=supported|refined
    return np.where(refined,disparity+correction,disparity).astype(np.float32),valid


def refine_stereo(images,disparities,masks):
    left,lv=refine_one(images[0],images[1],disparities[0],masks[0],masks[1])
    right,rv=refine_one(images[1][:,::-1],images[0][:,::-1],disparities[1][:,::-1],masks[1][:,::-1],masks[0][:,::-1])
    return (left,right[:,::-1].copy()),(masks[0]&lv,masks[1]&rv[:,::-1])
