import numpy as np
# glove-local coordinates in cm: u = thumb side, v = finger direction, n = back of hand
UV = {
 'hand':   (0.5,-11.0),
 'thumb1': (4.8,-9.6), 'thumb2': (7.0,-6.4), 'thumb3': (8.9,-3.9), 'thumb_tip': (10.3,-2.0),
 'index1': (2.5,-2.4), 'index_tip': (4.9,7.0),
 'middle1':(-0.3,-2.2),'middle_tip':(-0.2,8.2),
 'ring1':  (-3.1,-2.6),'ring_tip':  (-4.6,6.7),
 'pinky1': (-5.3,-3.4),'pinky_tip': (-7.3,2.6),
}
RATIO=(0.45,0.75)
def build(loc):
    """loc: Nx3 glove vertices in (u,v,n) cm. returns dict joint->3D point"""
    J={}
    def ncenter(u,v,r=1.0,mask=None):
        d=np.hypot(loc[:,0]-u,loc[:,1]-v)
        m=d<r
        if mask is not None: m&=mask
        if m.sum()<4:
            m=d<r*2
            if mask is not None: m&=mask
        nn=loc[m,2]
        return (nn.min()+nn.max())/2
    thumbmask=loc[:,0]>4.0
    for f in ['index','middle','ring','pinky']:
        a=np.array(UV[f+'1']); b=np.array(UV[f+'_tip'])
        pts={f+'1':a, f+'2':a+(b-a)*RATIO[0], f+'3':a+(b-a)*RATIO[1], f+'_tip':b-(b-a)*0.06}
        for k,p in pts.items():
            other=None
            J[k]=np.array([p[0],p[1],ncenter(p[0],p[1],0.8)])
    for k in ['thumb1','thumb2','thumb3','thumb_tip']:
        u,v=UV[k]
        if k=='thumb_tip':
            a=np.array(UV['thumb3']); b=np.array(UV['thumb_tip']); u,v=b-(b-a)*0.12
        J[k]=np.array([u,v,ncenter(u,v,0.9,thumbmask if k!='thumb1' else None)])
    u,v=UV['hand']; J['hand']=np.array([u,v,ncenter(u,v,1.5)])
    return J
