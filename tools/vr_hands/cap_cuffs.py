#!/usr/bin/env python3
"""Close the cut end of the VR gloves' forearm cuffs.

The cuff (bracer + straps) is a thin open shell cut a few centimetres above the wrist; the caps build_hands.py makes
only cover the cut loops it can find, so one could still look into the cuff. This adds one flat disc over the whole
cut end: the convex outline of the cuff's last 0.8 units, just inside the end, weighted to the hand joint.
Plain Python + numpy. Usage: cap_cuffs.py glove_left.md5mesh glove_right.md5mesh   (edits in place, idempotent)
"""
import re
import sys
import numpy as np

SHADER = 'vr/hands/cuff_end'
CUFF_MESH = 2          # 0 glove, 1 glove opening cap, 2 cuff, 3 cuff cut caps


def quat_w(q):
    return -np.sqrt(max(0.0, 1.0 - q @ q))


def rot(q, v):
    x, y, z, w = q
    qv = np.array([x, y, z])
    t = 2 * np.cross(qv, v)
    return v + w * t + np.cross(qv, t)


def hull(points):
    pts = sorted(map(tuple, points))
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return np.array(lower[:-1] + upper[:-1])


def process(path):
    s = open(path).read()
    if SHADER in s:
        print(path, 'already capped')
        return
    jt = re.search(r'joints \{(.*?)\n\}', s, re.S).group(1)
    joints = []
    for line in jt.strip().split('\n'):
        m = re.match(r'\s*"(\w+)"\s+(-?\d+)\s+\(\s*(\S+) (\S+) (\S+)\s*\)\s+\(\s*(\S+) (\S+) (\S+)\s*\)', line)
        name, _, *v = m.groups()
        v = list(map(float, v))
        q = np.array(v[3:])
        joints.append((name, np.array(v[:3]), np.array([q[0], q[1], q[2], quat_w(q)])))
    meshes = re.findall(r'mesh \{(.*?)\n\}', s, re.S)
    m = meshes[CUFF_MESH]
    verts = re.findall(r'vert \d+ \( \S+ \S+ \) (\d+) (\d+)', m)
    ws = re.findall(r'weight \d+ (\d+) (\S+) \( (\S+) (\S+) (\S+) \)', m)
    P = []
    for st, cnt in verts:
        st, cnt = int(st), int(cnt)
        p = np.zeros(3)
        for k in range(st, st + cnt):
            j, b, x, y, z = ws[k]
            _, jp, jq = joints[int(j)]
            p += float(b) * (jp + rot(jq, np.array([float(x), float(y), float(z)])))
        P.append(p)
    P = np.array(P)
    zmax = P[:, 2].max()
    ring = P[P[:, 2] > zmax - 0.8][:, :2]
    outline = hull(ring)
    zc = zmax - 0.12
    centre = outline.mean(0)
    cap = [np.array([centre[0], centre[1], zc])] + [np.array([x, y, zc]) for x, y in outline]
    n = len(outline)
    tris = [(0, 1 + i, 1 + (i + 1) % n) for i in range(n)]
    hand = [i for i, j in enumerate(joints) if j[0] == 'hand'][0]
    _, hp, hq = joints[hand]
    hq_inv = np.array([-hq[0], -hq[1], -hq[2], hq[3]])
    lines = ['mesh {', f'\tshader "{SHADER}"', '', f'\tnumverts {len(cap)}']
    lines += [f'\tvert {i} ( 0.500000 0.500000 ) {i} 1' for i in range(len(cap))]
    lines += ['', f'\tnumtris {len(tris)}']
    lines += [f'\ttri {i} {a} {b} {c}' for i, (a, b, c) in enumerate(tris)]
    lines += ['', f'\tnumweights {len(cap)}']
    for i, p in enumerate(cap):
        l = rot(hq_inv, p - hp)
        lines.append(f'\tweight {i} {hand} 1.000000 ( {l[0]:.6f} {l[1]:.6f} {l[2]:.6f} )')
    lines += ['}', '']
    s = re.sub(r'numMeshes (\d+)', lambda mm: f'numMeshes {int(mm.group(1)) + 1}', s, count=1)
    s = s.rstrip('\n') + '\n\n' + '\n'.join(lines)
    open(path, 'w').write(s)
    print(path, f'cuff end at z {zmax:.2f}: cap with {n} outline points')


if __name__ == '__main__':
    for p in sys.argv[1:]:
        process(p)
