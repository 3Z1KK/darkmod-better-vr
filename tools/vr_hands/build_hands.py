"""
Builds the VR hand models (MD5 mesh, left + mirrored right) from the glove glTF.
Source: "Glove" by Normal Map Enjoyer, CC-BY-4.0, https://sketchfab.com/3d-models/glove-7cb36be3202e41fcbb444cbf43d83745

usage: python3 build_hands.py <glove_low.glb> <glove_high.glb> <frame0.npy> <outdir>
Only the glove part is used (the bracer/laces are left out: floating hands, no arms).
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
from collections import defaultdict
from PIL import Image
from gltf import load
from joints import build

GAME_UNIT_CM = 2.309
GRIP_U, GRIP_V, GRIP_DEPTH = 0.5, float(os.environ.get('GRIP_V', '-1.5')), float(os.environ.get('GRIP_DEPTH', '2.0'))

# joint hierarchy: name, parent
JOINTS = [('origin', None), ('hand', 'origin')]
for f in ['thumb', 'index', 'middle', 'ring', 'pinky']:
    JOINTS += [(f + '1', 'hand'), (f + '2', f + '1'), (f + '3', f + '2')]
JOINTS += [('ring_socket', 'ring1')]   # where the light crystal ring sits
JNAMES = [j for j, _ in JOINTS]
JIDX = {j: i for i, j in enumerate(JNAMES)}

def quat_from_rows(A):
    """D3 convention: idQuat::ToMat3 gives A (rows = axes). Returns (x,y,z,w) with w >= 0"""
    m = A
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = np.sqrt(tr + 1.0) * 2
        w = 0.25 * s; x = (m[2, 1] - m[1, 2]) / s; y = (m[0, 2] - m[2, 0]) / s; z = (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w = (m[2, 1] - m[1, 2]) / s; x = 0.25 * s; y = (m[0, 1] + m[1, 0]) / s; z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w = (m[0, 2] - m[2, 0]) / s; x = (m[0, 1] + m[1, 0]) / s; y = 0.25 * s; z = (m[1, 2] + m[2, 1]) / s
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w = (m[1, 0] - m[0, 1]) / s; x = (m[0, 2] + m[2, 0]) / s; y = (m[1, 2] + m[2, 1]) / s; z = 0.25 * s
    q = np.array([x, y, z, w]); q /= np.linalg.norm(q)
    if q[3] < 0: q = -q
    return q

def mat_from_quat(q):
    x, y, z, w = q
    x2, y2, z2 = x + x, y + y, z + z
    xx, xy, xz = x * x2, x * y2, x * z2
    yy, yz, zz = y * y2, y * z2, z * z2
    wx, wy, wz = w * x2, w * y2, w * z2
    return np.array([[1 - (yy + zz), xy - wz, xz + wy], [xy + wz, 1 - (xx + zz), yz - wx], [xz - wy, yz + wx, 1 - (xx + yy)]])

def seg_dist(p, a, b):
    ab = b - a
    t = np.clip(((p - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
    return np.linalg.norm(p - (a + t[:, None] * ab), axis=1)

def main(low, high, frame, outdir):
    j, parts, imgs = load(low)
    g = [p for p in parts if p['name'].startswith('Glove')][0]
    F, W, N, c = np.load(frame)
    R = np.array([W, F, N])                        # model -> glove local (u, v, n)
    loc = ((g['pos'] - c) @ R.T) * 100             # cm
    nrm = g['nrm'] @ R.T
    tan = g['tan'][:, :3] @ R.T
    tsign = g['tan'][:, 3]
    uv = g['uv'].copy()
    tri = g['idx'].copy()

    J = build(loc)
    # extra joints
    J['origin'] = np.zeros(3)
    r1, r2 = J['ring1'], J['ring2']
    J['ring_socket'] = r1 + (r2 - r1) * 0.30

    # --- grip origin: centre of the controller handle in front of the palm ---
    # the handle runs through the ring of curled fingers: level with the knuckles, a handle radius below the palm
    palm_pt = np.array([GRIP_U, GRIP_V])
    m = np.hypot(loc[:, 0] - palm_pt[0], loc[:, 1] - palm_pt[1]) < 1.5
    palm_n = loc[m, 2].min()
    grip = np.array([palm_pt[0], palm_pt[1], palm_n - GRIP_DEPTH])
    print('palm surface n', palm_n, 'grip origin', grip)

    # --- cap the cuff opening ---
    key = np.round(loc * 1000).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
    E = defaultdict(list)
    for t in tri:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            E[(min(inv[a], inv[b]), max(inv[a], inv[b]))].append((a, b))
    bnd = [v[0] for k, v in E.items() if len(v) == 1]     # directed boundary edges (a->b as in the triangle)
    # follow the loop on welded vertices (the glTF splits vertices on UV seams)
    rep = {}
    for a, b in bnd:
        rep[inv[a]] = a; rep[inv[b]] = b
    nxt = {inv[a]: inv[b] for a, b in bnd}
    start = inv[bnd[0][0]]; wloop = [start]
    while nxt[wloop[-1]] != start:
        wloop.append(nxt[wloop[-1]])
    loop = np.array([rep[w] for w in wloop])
    cen = loc[loop].mean(0)
    cap_pos = np.vstack([loc[loop], cen[None]])
    k = len(loop)
    cap_tri_local = np.array([[(i + 1) % k, i, k] for i in range(k)])
    print('cap loop', k)

    # --- skin weights ---
    bones = []   # (joint, a, b)
    for f in ['index', 'middle', 'ring', 'pinky']:
        bones.append(('hand', J['hand'], J[f + '1']))
        bones += [(f + '1', J[f + '1'], J[f + '2']), (f + '2', J[f + '2'], J[f + '3']), (f + '3', J[f + '3'], J[f + '_tip'])]
    bones.append(('hand', J['hand'], J['thumb1']))
    bones += [('thumb1', J['thumb1'], J['thumb2']), ('thumb2', J['thumb2'], J['thumb3']), ('thumb3', J['thumb3'], J['thumb_tip'])]
    chain = lambda jn: jn.rstrip('123') if jn != 'hand' else 'hand'
    def weights(P):
        D = np.stack([seg_dist(P, a, b) for _, a, b in bones], 1)
        out = []
        for i in range(len(P)):
            d = D[i]
            order = np.argsort(d)
            nb = bones[order[0]][0]
            ch = chain(nb)
            if ch == 'hand':
                other = next(bones[o][0] for o in order if bones[o][0] != 'hand')
                ch = chain(other)
            allowed = [bi for bi, (jn, _, _) in enumerate(bones) if jn == 'hand' or chain(jn) == ch]
            best = {}
            dmin = d[allowed].min()
            for bi in allowed:
                jn = bones[bi][0]
                w = np.exp(-(d[bi] - dmin) / 0.35)
                best[jn] = max(best.get(jn, 0), w)
            items = sorted(best.items(), key=lambda x: -x[1])[:3]
            items = [(jn, w) for jn, w in items if w > 0.02]
            s = sum(w for _, w in items)
            out.append([(jn, w / s) for jn, w in items])
        return out
    W_glove = weights(loc)
    W_cap = [[('hand', 1.0)] for _ in range(len(cap_pos))]

    # --- joint frames (bind pose): x along the bone, bend axis y so +rotation about y curls towards the palm ---
    def frame_for(jn):
        if jn in ('origin',):
            return np.eye(3)
        if jn == 'hand':
            x = J['middle1'] - J['hand']; ref = np.array([0, 0, 1.0])      # back of hand
        elif jn == 'ring_socket':
            x = J['ring2'] - J['ring1']; ref = np.array([0, 0, 1.0])
        else:
            f = jn[:-1]; i = int(jn[-1])
            child = J[f + str(i + 1)] if i < 3 else J[f + '_tip']
            x = child - J[jn]
            if f == 'thumb':
                # the thumb curls towards the base of the index finger, across the palm
                target = J['index1'] + np.array([0, -1.0, -2.0])
                d = target - J[jn]; d -= x * (d @ x) / (x @ x)
                ref = -d
            else:
                ref = np.array([0, 0, 1.0])
        x = x / np.linalg.norm(x)
        z = ref - x * (ref @ x); z /= np.linalg.norm(z)
        y = np.cross(z, x)
        return np.array([x, y, z])
    frames = {jn: frame_for(jn) for jn in JNAMES}

    # --- convert to game hand space ---
    S = 1.0 / GAME_UNIT_CM
    def to_game(p, mirror):
        q = (p - grip) * S
        g_ = np.c_[q[..., 0], q[..., 2], -q[..., 1]] if q.ndim == 2 else np.array([q[0], q[2], -q[1]])
        if mirror:
            g_ = g_ * (np.array([1, -1, 1]) if g_.ndim == 1 else np.array([[1, -1, 1]]))
        return g_
    def dir_game(d, mirror):
        g_ = np.c_[d[..., 0], d[..., 2], -d[..., 1]] if d.ndim == 2 else np.array([d[0], d[2], -d[1]])
        if mirror:
            g_ = g_ * (np.array([1, -1, 1]) if g_.ndim == 1 else np.array([[1, -1, 1]]))
        return g_

    os.makedirs(os.path.join(outdir, 'models/vr/hands'), exist_ok=True)
    os.makedirs(os.path.join(outdir, 'textures/vr/hands'), exist_ok=True)

    for side, mirror in (('left', False), ('right', True)):
        jpos = {jn: to_game(J[jn], mirror) if jn != 'origin' else np.zeros(3) for jn in JNAMES}
        jaxes = {}
        for jn in JNAMES:
            A = np.array([dir_game(r, mirror) for r in frames[jn]])
            if mirror:
                # keep a proper rotation: flip the bend axis (y); curling stays "+rotation about y towards the palm"
                A[1] = -A[1]
                A[1] = np.cross(A[2], A[0])
            jaxes[jn] = A
        lines = ['MD5Version 10', 'commandline "VR hands, built by tools/vr_hands/build_hands.py"', '',
                 f'numJoints {len(JNAMES)}', 'numMeshes 2', '', 'joints {']
        for jn, par in JOINTS:
            p = jpos[jn]; q = quat_from_rows(jaxes[jn])
            assert np.allclose(mat_from_quat(q), jaxes[jn], atol=1e-4), jn
            pi = -1 if par is None else JIDX[par]
            lines.append(f'\t"{jn}"\t{pi} ( {p[0]:.6f} {p[1]:.6f} {p[2]:.6f} ) ( {q[0]:.6f} {q[1]:.6f} {q[2]:.6f} )')
        lines.append('}')

        def mesh_block(shader, P, UV, TRI, WTS):
            Pg = to_game(P, mirror)
            out = ['', 'mesh {', f'\tshader "{shader}"', '', f'\tnumverts {len(P)}']
            wl = []
            for i in range(len(P)):
                out.append(f'\tvert {i} ( {UV[i][0]:.6f} {UV[i][1]:.6f} ) {len(wl)} {len(WTS[i])}')
                for jn, w in WTS[i]:
                    off = jaxes[jn] @ (Pg[i] - jpos[jn])
                    wl.append((JIDX[jn], w, off))
            out += ['', f'\tnumtris {len(TRI)}']
            for i, t in enumerate(TRI):
                # idTech 4 front faces are wound the other way round; mirroring flips the winding again
                a, b, cc = (t[0], t[1], t[2]) if mirror else (t[0], t[2], t[1])
                out.append(f'\ttri {i} {a} {b} {cc}')
            out += ['', f'\tnumweights {len(wl)}']
            for i, (ji, w, off) in enumerate(wl):
                out.append(f'\tweight {i} {ji} {w:.6f} ( {off[0]:.6f} {off[1]:.6f} {off[2]:.6f} )')
            out.append('}')
            return out
        lines += mesh_block('vr/hands/glove', loc, uv, tri, W_glove)
        cap_uv = np.zeros((len(cap_pos), 2)) + 0.5
        lines += mesh_block('vr/hands/glove_inner', cap_pos, cap_uv, cap_tri_local, W_cap)
        path = os.path.join(outdir, f'models/vr/hands/glove_{side}.md5mesh')
        open(path, 'w').write('\n'.join(lines) + '\n')
        print('wrote', path)

    # --- textures from the high resolution source, at 2048 ---
    _, _, himgs = load(high)
    tex = j['materials'][0]['pbrMetallicRoughness']
    di = j['textures'][tex['baseColorTexture']['index']]['source']
    mi = j['textures'][tex['metallicRoughnessTexture']['index']]['source']
    ni = j['textures'][j['materials'][0]['normalTexture']['index']]['source']
    size = (2048, 2048)
    himgs[di].convert('RGB').resize(size, Image.LANCZOS).save(os.path.join(outdir, 'textures/vr/hands/glove_d.jpg'), quality=92)
    himgs[ni].convert('RGB').resize(size, Image.LANCZOS).save(os.path.join(outdir, 'textures/vr/hands/glove_local.jpg'), quality=95)
    mr = np.asarray(himgs[mi].convert('RGB').resize((1024, 1024), Image.LANCZOS), dtype=np.float32) / 255
    rough = mr[..., 1]; metal = mr[..., 2]
    spec = np.clip((1 - rough) ** 2 * 0.6 + metal * 0.3, 0, 1)
    print('roughness mean', rough.mean(), 'spec mean', spec.mean())
    Image.fromarray((spec * 255).astype(np.uint8)).convert('RGB').save(os.path.join(outdir, 'textures/vr/hands/glove_s.jpg'), quality=92)
    np.save(os.path.join(os.path.dirname(frame), 'rig_debug.npy'), dict(J=J, grip=grip), allow_pickle=True)

if __name__ == '__main__':
    main(*sys.argv[1:5])
