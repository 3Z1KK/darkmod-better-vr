"""
Builds the standalone VR bow from the game's first-person shortbow.

  python3 build_bow.py <shortbow.md5mesh> <idle.md5anim> <drawback.md5anim> <outdir>

The first-person model has three parts: the bow, the player's hand/arm and a bracer. Only the bow is kept,
with only its own bones (the riser, two limb chains of 6 bones, a tip bone per limb and the two string bones
that meet at the nock). The arm and bracer and their bones are dropped; no animation is cut.

The bow is re-rooted into hand space (the OpenXR grip pose, like the gloves): origin = centre of the grip,
+x = towards the upper limb tip (the thumb side of the fist), +z = towards the string (the draw direction,
towards the archer), -z = where the arrow flies. It is scaled up (the first-person bow is small).

Two poses are taken from the game's own animations: the bow at rest (idle) and at full draw (last frame of
the draw-back animation). The rest pose becomes the model's bind pose; the full-draw limb bend is written as
parent-relative joint values into the bow's entity def, and the game blends between the two by how far the
string is drawn. The string bones are moved by the game directly.
"""
import sys, os, re
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'vr_hands'))
from build_hands import mat_from_quat, quat_from_rows   # D3 conventions: matrices have the axes as rows
from md5check import parse

SCALE = 1.4

# vanilla bone -> new bone (parents follow the vanilla hierarchy)
RENAME = {
    'bowbone': 'riser',
    'joint15': 'up1', 'joint16': 'up2', 'joint17': 'up3', 'joint18': 'up4', 'joint19': 'up5', 'joint20': 'up6',
    'joint23': 'up_tip', 'joint24': 'nock_up',
    'joint9': 'lo1', 'joint10': 'lo2', 'joint11': 'lo3', 'joint12': 'lo4', 'joint13': 'lo5', 'joint14': 'lo6',
    'joint22': 'lo_tip', 'joint25': 'nock_lo',
}


def parse_anim(path):
    s = open(path).read()
    hier = []
    for l in re.search(r'hierarchy \{(.*?)\}', s, re.S).group(1).strip().split('\n'):
        m = re.match(r'\s*"([^"]+)"\s+(-?\d+)\s+(\d+)\s+(\d+)', l)
        hier.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))))
    base = [list(map(float, re.findall(r'-?[\d.]+(?:e-?\d+)?', l)))
            for l in re.search(r'baseframe \{(.*?)\}', s, re.S).group(1).strip().split('\n')]
    frames = [list(map(float, f.split())) for f in re.findall(r'frame \d+ \{(.*?)\}', s, re.S)]
    return hier, base, frames


def anim_world(anim, fi):
    """world pose (pos, rows-axes) of every joint in frame fi"""
    hier, base, frames = anim
    fr = frames[fi]
    out = []
    for i, (n, p, flags, start) in enumerate(hier):
        b = list(base[i])
        k = 0
        for c in range(6):
            if flags & (1 << c):
                b[c] = fr[start + k]
                k += 1
        x, y, z = b[3:6]
        w = np.sqrt(max(0.0, 1 - x * x - y * y - z * z))
        lA = mat_from_quat((x, y, z, w))
        lp = np.array(b[:3])
        if p < 0:
            out.append((lp, lA))
        else:
            pp, pA = out[p]
            out.append((pp + lp @ pA, lA @ pA))
    return out


def skin(mesh, posed):
    P = []
    for s, t, ws, wc in mesh['verts']:
        v = np.zeros(3)
        for k in range(ws, ws + wc):
            j, b, o = mesh['wts'][k]
            jp, jA = posed[j]
            v += b * (jp + o @ jA)
        P.append(v)
    return np.array(P)


def main(meshPath, idlePath, drawPath, outdir):
    joints, meshes = parse(meshPath)
    names = [j[0] for j in joints]
    bow = [m for m in meshes if m['shader'].endswith('shortbow/shortbow')][0]
    idle = parse_anim(idlePath)
    draw = parse_anim(drawPath)
    assert [h[0] for h in idle[0]] == names and [h[0] for h in draw[0]] == names

    rest = anim_world(idle, 0)
    full = anim_world(draw, len(draw[2]) - 1)
    J = {n: i for i, n in enumerate(names)}

    # full draw relative to the riser, placed on the riser of the rest pose
    bb = J['bowbone']
    full_rel = []
    for i in range(len(names)):
        p, A = full[i]
        Arel = A @ full[bb][1].T
        prel = (p - full[bb][0]) @ full[bb][1].T
        full_rel.append((rest[bb][0] + prel @ rest[bb][1], Arel @ rest[bb][1]))

    # bow frame from the rest pose
    restVerts = skin(bow, rest)
    dom = []
    for s, t, ws, wc in bow['verts']:
        w = max(range(ws, ws + wc), key=lambda k: bow['wts'][k][1])
        dom.append(names[bow['wts'][w][0]])
    dom = np.array(dom)
    riserVerts = restVerts[np.isin(dom, ['bowbone', 'joint9', 'joint15'])]
    tipUp = rest[J['joint23']][0]
    tipLo = rest[J['joint22']][0]
    nock = (rest[J['joint24']][0] + rest[J['joint25']][0]) * 0.5
    x = tipUp - tipLo
    x /= np.linalg.norm(x)
    # the grip: the riser's centre, on the line through it along the limbs
    grip = riserVerts.mean(0)
    zf = nock - grip
    z = zf - (zf @ x) * x
    z /= np.linalg.norm(z)
    y = np.cross(z, x)
    B = np.array([x, y, z])

    def tp(p):
        return SCALE * ((p - grip) @ B.T)

    def ta(A):
        return A @ B.T

    keep = [n for n in names if n in RENAME]
    newNames = ['origin'] + [RENAME[n] for n in keep]
    parentOf = {}
    for n in keep:
        p = joints[J[n]][1]
        pn = names[p]
        parentOf[RENAME[n]] = RENAME.get(pn, 'origin')
    newIdx = {n: i for i, n in enumerate(newNames)}
    remap = {J[n]: newIdx[RENAME[n]] for n in keep}

    restNew = {'origin': (np.zeros(3), np.eye(3))}
    fullNew = {'origin': (np.zeros(3), np.eye(3))}
    for n in keep:
        restNew[RENAME[n]] = (tp(rest[J[n]][0]), ta(rest[J[n]][1]))
        fullNew[RENAME[n]] = (tp(full_rel[J[n]][0]), ta(full_rel[J[n]][1]))

    # weights: only bow bones are used by the bow mesh
    for j, b, o in bow['wts']:
        assert j in remap, names[j]

    os.makedirs(os.path.join(outdir, 'models/vr/bow'), exist_ok=True)
    lines = ['MD5Version 10', 'commandline "VR bow, built by tools/vr_bow/build_bow.py from the first-person shortbow"', '',
             f'numJoints {len(newNames)}', 'numMeshes 1', '', 'joints {']
    for n in newNames:
        p, A = restNew[n]
        q = quat_from_rows(A)
        pi = -1 if n == 'origin' else newIdx[parentOf[n]]
        lines.append(f'\t"{n}"\t{pi} ( {p[0]:.6f} {p[1]:.6f} {p[2]:.6f} ) ( {q[0]:.6f} {q[1]:.6f} {q[2]:.6f} )')
    lines += ['}', '', 'mesh {', f'\tshader "{bow["shader"]}"', '', f'\tnumverts {len(bow["verts"])}']
    for i, (s, t, ws, wc) in enumerate(bow['verts']):
        lines.append(f'\tvert {i} ( {s:.6f} {t:.6f} ) {ws} {wc}')
    lines += ['', f'\tnumtris {len(bow["tris"])}']
    for i, (a, b, c) in enumerate(bow['tris']):
        lines.append(f'\ttri {i} {a} {b} {c}')
    lines += ['', f'\tnumweights {len(bow["wts"])}']
    for i, (j, b, o) in enumerate(bow['wts']):
        o2 = o * SCALE
        lines.append(f'\tweight {i} {remap[j]} {b:.6f} ( {o2[0]:.6f} {o2[1]:.6f} {o2[2]:.6f} )')
    lines += ['}', '']
    open(os.path.join(outdir, 'models/vr/bow/vr_bow.md5mesh'), 'w').write('\n'.join(lines))

    # full-draw pose as parent-relative joint values (what the engine's default pose holds for the rest pose)
    def local(pose, n):
        p, A = pose[n]
        pp, pA = pose[parentOf[n]]
        return (p - pp) @ pA.T, A @ pA.T

    full_args = []
    for n in newNames[1:]:
        t, A = local(fullNew, n)
        q = quat_from_rows(A)
        full_args.append((n, t, q))

    restNock = (restNew['nock_up'][0] + restNew['nock_lo'][0]) * 0.5
    fullNock = (fullNew['nock_up'][0] + fullNew['nock_lo'][0]) * 0.5
    info = dict(
        tip_to_tip=np.linalg.norm(restNew['up_tip'][0] - restNew['lo_tip'][0]),
        tip_to_tip_full=np.linalg.norm(fullNew['up_tip'][0] - fullNew['lo_tip'][0]),
        rest_nock=restNock, full_nock=fullNock,
        anim_draw=fullNock[2] - restNock[2],
    )
    return newNames, restNew, fullNew, full_args, info, bow


DEF_HEAD = '''// VR bow and arrows. Generated by tools/vr_bow/build_bow.py - edit the script, not this file.
//
// atdm:vr_bow is not spawned: it holds the data of the VR bow (game/vr/VRBow.cpp). The bow model is the bow
// part of the first-person shortbow (models/md5/weapons/shortbow/shortbow.md5mesh, The Dark Mod team,
// CC BY-NC-SA), without the hand, arm and bracer, scaled up and re-rooted at the grip.
// Bow space = hand space: origin in the fist, +x towards the upper limb, +z towards the string (the archer).

entityDef atdm:vr_bow
{
	"editor_usage"		"VR only: data of the hand-held bow. Not placed in maps; bows placed in maps turn into it when grabbed."
	"editor_displayFolder"	"Internal/VR"
	"model"				"models/vr/bow/vr_bow.md5mesh"
'''

DEF_TAIL = '''}

// An arrow in the hand (spawned at the right shoulder for now). It is a broadhead pickup: let go at the
// stomach it goes into the inventory like one. The model points along +z, tip at the origin.
entityDef atdm:vr_arrow_broadhead
{
	"inherit"			"atdm:ammo_broadhead"
	"editor_usage"		"VR only: a broadhead arrow held in the hand, nocked on the VR bow by touching the bow with it."
	"editor_displayFolder"	"Internal/VR"
	"vr_arrow"			"broadhead"		// shot as this arrow weapon's projectile (atdm:weapon_<name>)
	"vr_weightless"		"1"
	"grabable"			"1"
	"frobable"			"1"
}
'''


if __name__ == '__main__':
    newNames, restNew, fullNew, full_args, info, bow = main(*sys.argv[1:5])
    for k, v in info.items():
        print(k, np.round(v, 3) if isinstance(v, np.ndarray) else round(float(v), 3))
    # bounds of the bow at rest, in bow space
    joints, meshes = parse(os.path.join(sys.argv[4], 'models/vr/bow/vr_bow.md5mesh'))
    P = skin(meshes[0], [(j[2], j[3]) for j in joints])
    lo, hi = P.min(0), P.max(0)
    rn = info['rest_nock']
    out = [DEF_HEAD.rstrip('\n'),
           f'\t"vr_bounds_min"\t\t"{lo[0]:.2f} {lo[1]:.2f} {lo[2]:.2f}"',
           f'\t"vr_bounds_max"\t\t"{hi[0]:.2f} {hi[1]:.2f} {hi[2]:.2f}"',
           f'\t"vr_rest_nock"\t\t"{rn[0]:.3f} {rn[1]:.3f} {rn[2]:.3f}"\t// the string middle at rest',
           f'\t"vr_anim_draw"\t\t"{info["anim_draw"]:.3f}"\t// how far the full-draw pose below is drawn',
           '\t"vr_arrow_rest"\t\t"2.4 0.6 -0.3"\t// where the arrow lies on the bow: just above the fist, beside the riser',
           '\t"vr_nock_height"\t\t"2.4"\t// the nocking point on the string (same height as the rest)',
           '\t"snd_take"\t\t\t"bow_unsheath"',
           '\t"snd_put"\t\t\t"bow_sheath"',
           '\t"snd_draw"\t\t\t"bow_draw"',
           '\t"snd_nock"\t\t\t"arrow_equip"',
           '\t"snd_release"\t\t"shortbow_fire"',
           '',
           '\t// full draw: every bone relative to its parent (offset, then quaternion x y z w)']
    for n, t, q in full_args:
        out.append(f'\t"vr_full_{n}"\t\t"{t[0]:.5f} {t[1]:.5f} {t[2]:.5f} {q[0]:.6f} {q[1]:.6f} {q[2]:.6f} {q[3]:.6f}"')
    os.makedirs(os.path.join(sys.argv[4], 'def'), exist_ok=True)
    open(os.path.join(sys.argv[4], 'def/tdm_vr_bow.def'), 'w').write('\n'.join(out) + '\n' + DEF_TAIL)
    print('bounds', np.round(lo, 2), np.round(hi, 2))
