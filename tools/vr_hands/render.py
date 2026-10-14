import numpy as np
from PIL import Image, ImageDraw
def render(parts, view_axes, size=600, tex=None, highlight=None, pts=None, lines=None, labels=None, center=None, scale=None):
    """orthographic render. view_axes: 3x3 rows = right, up, toward viewer (in model space)"""
    R=np.array(view_axes,dtype=np.float64)
    allp=np.vstack([p['pos'] for p in parts])
    if center is None: center=(allp.min(0)+allp.max(0))/2
    if scale is None: scale=(size*0.9)/np.max(np.ptp(allp@R.T,axis=0)[:2])
    W=H=size
    zbuf=np.full((H,W),-1e9); img=np.zeros((H,W,3),np.float32)+0.15
    light=np.array([0.3,0.5,0.8]); light/=np.linalg.norm(light)
    texa=np.asarray(tex,dtype=np.float32)/255 if tex is not None else None
    def proj(p): 
        q=(p-center)@R.T
        return np.c_[q[:,0]*scale+W/2, -q[:,1]*scale+H/2, q[:,2]]
    for pi,p in enumerate(parts):
        v=proj(p['pos']); tri=p['idx']
        a,b,c=v[tri[:,0]],v[tri[:,1]],v[tri[:,2]]
        n=np.cross((p['pos'][tri[:,1]]-p['pos'][tri[:,0]])@R.T,(p['pos'][tri[:,2]]-p['pos'][tri[:,0]])@R.T)
        nl=np.linalg.norm(n,axis=1)+1e-12; n/=nl[:,None]
        shade=np.abs(n@light)*0.8+0.2
        if texa is not None:
            uvc=(p['uv'][tri[:,0]]+p['uv'][tri[:,1]]+p['uv'][tri[:,2]])/3
            tx=(np.clip(uvc[:,0]%1,0,0.999)*texa.shape[1]).astype(int); ty=(np.clip(uvc[:,1]%1,0,0.999)*texa.shape[0]).astype(int)
            col=texa[ty,tx]
        else: col=np.tile(np.array([0.8,0.7,0.6]),(len(tri),1))
        if highlight is not None and pi in highlight: col=col*0.3+np.array(highlight[pi])*0.7
        for t in range(len(tri)):
            xs=np.array([a[t,0],b[t,0],c[t,0]]); ys=np.array([a[t,1],b[t,1],c[t,1]])
            x0,x1=int(max(0,np.floor(xs.min()))),int(min(W-1,np.ceil(xs.max())))
            y0,y1=int(max(0,np.floor(ys.min()))),int(min(H-1,np.ceil(ys.max())))
            if x1<x0 or y1<y0: continue
            gx,gy=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
            d=(b[t,0]-a[t,0])*(c[t,1]-a[t,1])-(c[t,0]-a[t,0])*(b[t,1]-a[t,1])
            if abs(d)<1e-9: continue
            w1=((gx-a[t,0])*(c[t,1]-a[t,1])-(c[t,0]-a[t,0])*(gy-a[t,1]))/d
            w2=((b[t,0]-a[t,0])*(gy-a[t,1])-(gx-a[t,0])*(b[t,1]-a[t,1]))/d
            w0=1-w1-w2
            m=(w0>=-1e-4)&(w1>=-1e-4)&(w2>=-1e-4)
            if not m.any(): continue
            z=w0*a[t,2]+w1*b[t,2]+w2*c[t,2]
            sub=zbuf[y0:y1+1,x0:x1+1]; upd=m&(z>sub)
            sub[upd]=z[upd]
            img[y0:y1+1,x0:x1+1][upd]=col[t]*shade[t]
    im=Image.fromarray((np.clip(img,0,1)*255).astype(np.uint8))
    dr=ImageDraw.Draw(im)
    if lines is not None:
        for (p0,p1,colr) in lines:
            q=proj(np.array([p0,p1])); dr.line([tuple(q[0,:2]),tuple(q[1,:2])],fill=colr,width=3)
    if pts is not None:
        q=proj(np.array([x[0] for x in pts]))
        for (pp,colr,lab),qq in zip(pts,q):
            dr.ellipse([qq[0]-5,qq[1]-5,qq[0]+5,qq[1]+5],fill=colr)
            if lab: dr.text((qq[0]+7,qq[1]-7),lab,fill=(255,255,0))
    return im, (center, scale)
