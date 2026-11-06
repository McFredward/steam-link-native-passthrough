"""Subpixel photometric verification around neural disparity proposals."""
import numpy as np
from scipy.ndimage import uniform_filter,minimum_filter


def context_support(left,right,disparity,left_mask,right_mask):
    """Verify an unchanged proposal against observed, warped image context.

    A five-pixel texture test mostly measures sensor noise on smooth room
    surfaces. Larger windows can contain real structure even when the centre
    is smooth. They verify the neural proposal; they never extrapolate a new
    disparity or grant an unobserved centre. A completely blank window still
    has no matching evidence.
    """
    h,w=left.shape;y,x=np.mgrid[:h,:w]
    coordinate=x-disparity
    # Centre intensities before the variance calculation to retain precision
    # on low-contrast input. Warp before measuring correlation so a sloping
    # measured plane does not need a constant disparity across the window.
    l=left.astype(np.float32)-128
    fields=[]
    for offset in (0.,-4*w/320,4*w/320):
        u=coordinate-offset
        lo=np.clip(np.floor(u).astype(np.int32),0,w-2);t=np.clip(u-lo,0,1)
        r=(right[y,lo]*(1-t)+right[y,lo+1]*t).astype(np.float32)-128
        support=left_mask&right_mask[y,lo]&right_mask[y,lo+1]&(u>=0)&(u<w-1)
        fields.append((support,np.where(support,l,0),np.where(support,r,0)))
    centre=minimum_filter(fields[0][0].astype(np.uint8),5,mode='constant').astype(bool)
    qualified=np.zeros((h,w),bool)
    for size in (15,31):
        window=max(5,int(round(size*w/320))|1)
        average=lambda value:uniform_filter(value,window,mode='constant')
        scores=[]
        for support,a,b in fields:
            weight=average(support.astype(np.float32))
            denominator=np.maximum(weight,1e-6)
            ml,mr=average(a)/denominator,average(b)/denominator
            vl=np.maximum(0,average(a*a)/denominator-ml*ml)
            vr=np.maximum(0,average(b*b)/denominator-mr*mr)
            covariance=average(a*b)/denominator-ml*mr
            correlation=covariance/np.maximum(np.sqrt(vl*vr),1e-6)
            scores.append(np.where((weight>.95)&(np.minimum(vl,vr)>=2.5**2),correlation,-1))
        # A gain/offset-normalized linear brightness ramp matches any depth.
        # Require actual evidence against displaced alternatives as well.
        qualified|=centre&(scores[0]>.85)&(scores[0]>np.maximum(scores[1],scores[2])+.01)
    return qualified


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
    valid=supported|refined|context_support(left,right,disparity,left_mask,right_mask)
    return np.where(refined,disparity+correction,disparity).astype(np.float32),valid


def refine_stereo(images,disparities,masks):
    left,lv=refine_one(images[0],images[1],disparities[0],masks[0],masks[1])
    right,rv=refine_one(images[1][:,::-1],images[0][:,::-1],disparities[1][:,::-1],masks[1][:,::-1],masks[0][:,::-1])
    return (left,right[:,::-1].copy()),(masks[0]&lv,masks[1]&rv[:,::-1])
