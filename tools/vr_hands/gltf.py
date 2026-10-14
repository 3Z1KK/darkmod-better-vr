import json,struct,io
import numpy as np
from PIL import Image
CT={5126:np.float32,5125:np.uint32,5123:np.uint16,5121:np.uint8}
NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def load(f):
    b=open(f,'rb').read()
    clen=struct.unpack('<I',b[12:16])[0]
    j=json.loads(b[20:20+clen]); off=20+clen
    blen=struct.unpack('<I',b[off:off+4])[0]; bin_=b[off+8:off+8+blen]
    def acc(i):
        a=j['accessors'][i]; bv=j['bufferViews'][a['bufferView']]
        o=bv.get('byteOffset',0)+a.get('byteOffset',0); n=NC[a['type']]; dt=CT[a['componentType']]
        stride=bv.get('byteStride',0); isz=np.dtype(dt).itemsize*n
        if stride and stride!=isz:
            raw=np.frombuffer(bin_,dtype=np.uint8,count=stride*a['count'],offset=o).reshape(a['count'],stride)[:,:isz]
            return np.frombuffer(raw.tobytes(),dtype=dt).reshape(a['count'],n)
        return np.frombuffer(bin_,dtype=dt,count=a['count']*n,offset=o).reshape(a['count'],n)
    def mat(n):
        if 'matrix' in n: return np.array(n['matrix'],dtype=np.float64).reshape(4,4).T
        M=np.eye(4)
        if 'scale' in n: M=np.diag(list(n['scale'])+[1])@M
        if 'rotation' in n:
            x,y,z,w=n['rotation']
            R=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
            M2=np.eye(4); M2[:3,:3]=R; M=M2@M
        if 'translation' in n: M[:3,3]+=n['translation']
        return M
    parts=[]
    def walk(ni,P):
        n=j['nodes'][ni]; M=P@mat(n)
        if 'mesh' in n:
            for p in j['meshes'][n['mesh']]['primitives']:
                A=p['attributes']
                pos=acc(A['POSITION']).astype(np.float64)
                nrm=acc(A['NORMAL']).astype(np.float64)
                tan=acc(A['TANGENT']).astype(np.float64) if 'TANGENT' in A else None
                uv=acc(A['TEXCOORD_0']).astype(np.float64)
                idx=acc(p['indices']).reshape(-1,3).astype(np.int64)
                P4=np.c_[pos,np.ones(len(pos))]@M.T
                N3=nrm@np.linalg.inv(M[:3,:3]).T
                N3/=np.linalg.norm(N3,axis=1,keepdims=True)+1e-12
                T3=None
                if tan is not None:
                    T3=tan[:,:3]@M[:3,:3].T; T3/=np.linalg.norm(T3,axis=1,keepdims=True)+1e-12
                    T3=np.c_[T3,tan[:,3]*np.sign(np.linalg.det(M[:3,:3]))]
                if np.linalg.det(M[:3,:3])<0: idx=idx[:,::-1]
                parts.append(dict(name=n.get('name') or j['meshes'][n['mesh']].get('name'),pos=P4[:,:3],nrm=N3,tan=T3,uv=uv,idx=idx))
        for c in n.get('children',[]): walk(c,M)
    for r in j['scenes'][j.get('scene',0)]['nodes']: walk(r,np.eye(4))
    imgs=[]
    for im in j['images']:
        bv=j['bufferViews'][im['bufferView']]; o=bv.get('byteOffset',0)
        imgs.append(Image.open(io.BytesIO(bin_[o:o+bv['byteLength']])))
    return j,parts,imgs
