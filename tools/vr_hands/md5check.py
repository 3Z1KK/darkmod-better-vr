import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
from build_hands import mat_from_quat
def parse(path):
    txt=open(path).read()
    joints=[]
    jb=txt[txt.index('joints {')+8:txt.index('}',txt.index('joints {'))]
    for l in jb.strip().split('\n'):
        m=re.match(r'\s*"(\w+)"\s+(-?\d+)\s+\(\s*(\S+) (\S+) (\S+)\s*\)\s+\(\s*(\S+) (\S+) (\S+)\s*\)',l)
        n,p,*f=m.groups(); f=list(map(float,f))
        x,y,z=f[3:]; w=np.sqrt(max(0,1-x*x-y*y-z*z))
        joints.append((n,int(p),np.array(f[:3]),mat_from_quat((x,y,z,w))))
    meshes=[]
    for mb in re.findall(r'mesh \{(.*?)\n\}',txt,re.S):
        shader=re.search(r'shader "(.*?)"',mb).group(1)
        verts=[(float(a),float(b),int(c),int(d)) for a,b,c,d in re.findall(r'vert \d+ \( (\S+) (\S+) \) (\d+) (\d+)',mb)]
        tris=np.array([list(map(int,t)) for t in re.findall(r'tri \d+ (\d+) (\d+) (\d+)',mb)])
        wts=[(int(j),float(b),np.array(list(map(float,(x,y,z))))) for j,b,x,y,z in re.findall(r'weight \d+ (\d+) (\S+) \( (\S+) (\S+) (\S+) \)',mb)]
        meshes.append(dict(shader=shader,verts=verts,tris=tris,wts=wts))
    return joints,meshes
def pose(joints, curls):
    """curls: joint name -> angle (deg) about its local y. returns world (pos, axes) per joint"""
    out=[]
    for i,(n,p,pos,A) in enumerate(joints):
        if p<0: out.append((pos,A)); continue
        ppos,pA=joints[p][2],joints[p][3]
        lpos=pA@(pos-ppos); lA=A@pA.T          # relative to parent (bind)
        a=np.radians(curls.get(n,0.0))
        # rotate about local y: rows rotate; x towards -z for +a
        Ry=np.array([[np.cos(a),0,-np.sin(a)],[0,1,0],[np.sin(a),0,np.cos(a)]])
        lA2=Ry@lA
        wp,wA=out[p]
        out.append((wp+lpos@wA, lA2@wA))
    return out
def skin(joints, mesh, posed):
    P=[]
    for s,t,ws,wc in mesh['verts']:
        v=np.zeros(3)
        for k in range(ws,ws+wc):
            j,b,o=mesh['wts'][k]; jp,jA=posed[j]; v+=b*(jp+o@jA)
        P.append(v)
    return np.array(P)
