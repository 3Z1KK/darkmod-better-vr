#!/usr/bin/env python3
"""The VR menu room: a small old motel room (maps/vr_menu.map).

Brushes for the shell (floor, walls with wainscot, ceiling with beams, a window), stock TDM models for the
furniture, a rocking chair built from brushes (TDM has no rocking chair model), lights and fire.

The map is meant to be drawn without the game running (the VR main menu draws its geometry, models, lights and
particles), so it uses only plain entities: func_static models, lights (a light's "model" is its flame particle),
a speaker. No fireplace light-holder entities with attachments: their parts are placed one by one.

Plain Python. Usage: build_menu_room.py <out.map>
Room interior: x 0..224 (west: door, east: fireplace), y 0..176 (south: bed, north: window), z 0..128.
1 unit = 2.3 cm.
"""
import math
import sys

CAULK = 'textures/common/caulk'
FLOOR = 'textures/darkmod/wood/boards/worn_01'
WALL_LOW = 'textures/darkmod/wood/panels/wood_brown_dull01_panel'
WALL_HIGH = 'textures/darkmod/paint_paper/wallpaper_victorian_01'
TRIM = 'textures/darkmod/wood/panels/molding_walnut'
CEILING = 'textures/darkmod/plaster/tiling_1d/plaster_dirty01_grey'
BEAM = 'textures/darkmod/wood/boards/rough_wood_darkbrown_dull'
WINDOW = 'textures/darkmod/window/wooden_frame01/wooden_frame01_moonlit'
CHAIR_WOOD = 'textures/darkmod/wood/boards/rough_wood_darkbrown_dull'

X0, X1, Y0, Y1, Z0, Z1 = 0, 224, 0, 176, 0, 128
T = 8           # shell thickness
RAIL = 40       # wainscot height


def axis_base(n):
    """idlib/MapFile.cpp ComputeAxisBase"""
    nx, ny, nz = [0.0 if abs(c) < 1e-6 else c for c in n]
    rot_y = -math.atan2(nz, math.sqrt(ny * ny + nx * nx))
    rot_z = math.atan2(ny, nx)
    s = (-math.sin(rot_z), math.cos(rot_z), 0.0)
    t = (-math.sin(rot_y) * math.cos(rot_z), -math.sin(rot_y) * math.sin(rot_z), -math.cos(rot_y))
    return s, t


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    l = math.sqrt(dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l)


class Face:
    def __init__(self, normal, dist, material, tile=128.0, fit=None):
        self.n, self.d, self.mat, self.tile, self.fit = normal, dist, material, tile, fit

    def text(self):
        s, t = axis_base(self.n)
        if self.fit:
            # map the face's extent exactly onto the texture once (corners: list of points on the face)
            us = [dot(p, s) for p in self.fit]
            vs = [dot(p, t) for p in self.fit]
            u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
            m = ((1 / (u1 - u0), 0, -u0 / (u1 - u0)), (0, 1 / (v1 - v0), -v0 / (v1 - v0)))
        else:
            k = 1.0 / self.tile
            m = ((k, 0, 0), (0, k, 0))
        n = self.n
        return '( %g %g %g %g ) ( ( %.7g %.7g %.7g ) ( %.7g %.7g %.7g ) ) "%s" 0 0 0' % (
            n[0], n[1], n[2], -self.d, m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], self.mat)


def box(lo, hi, mats, tile=128.0, fit=None):
    """axis-aligned box; mats: material for all faces, or dict by side (-x,+x,-y,+y,-z,+z), default caulk"""
    sides = {'-x': ((-1, 0, 0), -lo[0]), '+x': ((1, 0, 0), hi[0]), '-y': ((0, -1, 0), -lo[1]),
             '+y': ((0, 1, 0), hi[1]), '-z': ((0, 0, -1), -lo[2]), '+z': ((0, 0, 1), hi[2])}
    faces = []
    for k, (n, d) in sides.items():
        m = mats if isinstance(mats, str) else mats.get(k, CAULK)
        f = None
        if fit and k in fit:
            f = fit[k]
        faces.append(Face(n, d, m, tile, f))
    return faces


def obox(centre, ax, ay, az, half, mat, tile=64.0):
    """oriented box: centre, unit axes, half extents"""
    faces = []
    for a, h in ((ax, half[0]), (ay, half[1]), (az, half[2])):
        for sgn in (1, -1):
            n = (a[0] * sgn, a[1] * sgn, a[2] * sgn)
            faces.append(Face(n, dot(n, centre) + h, mat, tile))
    return faces


def brush_text(faces, idx):
    return '// primitive %d\n{\nbrushDef3\n{\n%s\n}\n}\n' % (idx, '\n'.join(f.text() for f in faces))


def entity(idx, keys, brushes=()):
    out = '// entity %d\n{\n' % idx
    for k, v in keys:
        out += '"%s" "%s"\n' % (k, v)
    for i, b in enumerate(brushes):
        out += brush_text(b, i)
    return out + '}\n'


def shell():
    b = []
    # floor and ceiling
    b.append(box((X0 - T, Y0 - T, Z0 - T), (X1 + T, Y1 + T, Z0), {'+z': FLOOR}))
    b.append(box((X0 - T, Y0 - T, Z1), (X1 + T, Y1 + T, Z1 + T), {'-z': CEILING}))
    # walls: wainscot panels below the rail, wallpaper above, each wall two brushes
    walls = [
        ('+x', (X0 - T, Y0 - T, Z0), (X0, Y1 + T, Z1)),      # west (over the corners, so the room is sealed)
        ('-x', (X1, Y0 - T, Z0), (X1 + T, Y1 + T, Z1)),      # east
        ('+y', (X0, Y0 - T, Z0), (X1, Y0, Z1)),      # south
        ('-y', (X0, Y1, Z0), (X1, Y1 + T, Z1)),      # north
    ]
    for side, lo, hi in walls:
        b.append(box(lo, (hi[0], hi[1], RAIL), {side: WALL_LOW}, tile=64.0))
        b.append(box((lo[0], lo[1], RAIL), hi, {side: WALL_HIGH}, tile=96.0))
        # corners closed: the walls run over the full floor plan; the chair rail sticks out one unit
        tl, th = list(lo), list(hi)
        axis = {'+x': 0, '-x': 0, '+y': 1, '-y': 1}[side]
        inward = 1 if side[0] == '+' else -1
        face = hi[axis] if inward > 0 else lo[axis]
        tl[axis], th[axis] = (face, face + 1.2) if inward > 0 else (face - 1.2, face)
        tl[2], th[2] = RAIL - 1.5, RAIL + 2.5
        if axis == 0:
            tl[1], th[1] = Y0, Y1       # east/west walls run over the corners; their rail only along the room
        b.append(box(tuple(tl), tuple(th), TRIM, tile=32.0))
    # the window on the north wall (a moonlit window texture over the wall face)
    wx0, wx1, wz0, wz1 = 84, 140, 46, 110
    corners = [(wx0, Y1 - 0.5, wz0), (wx1, Y1 - 0.5, wz0), (wx0, Y1 - 0.5, wz1), (wx1, Y1 - 0.5, wz1)]
    b.append(box((wx0, Y1 - 0.5, wz0), (wx1, Y1, wz1), {'-y': WINDOW}, fit={'-y': corners}))
    # ceiling beams (north-south)
    for x in (56, 112, 168):
        b.append(box((x - 5, Y0, Z1 - 9), (x + 5, Y1, Z1), BEAM, tile=64.0))
    return b


def rocking_chair():
    """facing +x, seat centre at the origin's x/y; built in world coordinates later by offset"""
    parts = []
    w = 9.5      # runner y offset
    # runners: an arc of radius 46 under the chair, 6 segments
    r, half_angle, n = 46.0, math.radians(26), 6
    pts = []
    for i in range(n + 1):
        a = -half_angle + 2 * half_angle * i / n
        pts.append((r * math.sin(a), r - r * math.cos(a) + 1.0))
    for y in (-w, w):
        for (xa, za), (xb, zb) in zip(pts, pts[1:]):
            d = norm((xb - xa, 0, zb - za))
            up = cross((0, 1, 0), d)
            if up[2] < 0:
                up = (-up[0], -up[1], -up[2])
            c = ((xa + xb) / 2, y, (za + zb) / 2 + 0.9)
            l = math.hypot(xb - xa, zb - za) / 2 + 0.4
            parts.append(obox(c, d, (0, 1, 0), up, (l, 0.7, 0.9), CHAIR_WOOD))
    # legs from the runners to the seat
    for x in (-8, 8):
        for y in (-w, w):
            zr = r - math.sqrt(max(0, r * r - x * x)) + 1.0
            parts.append(box((x - 1, y - 1, zr + 1), (x + 1, y + 1, 18), CHAIR_WOOD, tile=64.0))
    # seat
    parts.append(box((-11, -11, 18), (11, 11, 20), CHAIR_WOOD, tile=64.0))
    # back (on the -x side, leaning back a little: built from slanted posts and slats)
    lean = math.radians(12)
    up = (-math.sin(lean), 0, math.cos(lean))
    fwd = (math.cos(lean), 0, math.sin(lean))
    base = (-10.5, 0, 20)
    def along(h):
        return (base[0] + up[0] * h, 0, base[2] + up[2] * h)
    for y in (-w, w):
        c = along(14)
        parts.append(obox((c[0], y, c[2]), fwd, (0, 1, 0), up, (1.0, 1.0, 14.5), CHAIR_WOOD))
    for y in (-5, 0, 5):
        c = along(13)
        parts.append(obox((c[0], y, c[2]), fwd, (0, 1, 0), up, (0.5, 1.6, 10), CHAIR_WOOD))
    for h in (4, 25):
        c = along(h)
        parts.append(obox((c[0], 0, c[2]), fwd, (0, 1, 0), up, (0.7, w + 0.5, 1.3), CHAIR_WOOD))
    # arm rests and their front posts
    for y in (-w - 1, w + 1):
        parts.append(box((-10, y - 1, 30), (9, y + 1, 31.6), CHAIR_WOOD, tile=64.0))
        parts.append(box((6.5, y - 0.8, 20), (8.1, y + 0.8, 30), CHAIR_WOOD, tile=64.0))
    return parts


def offset_faces(faces, o):
    out = []
    for f in faces:
        g = Face(f.n, f.d + dot(f.n, o), f.mat, f.tile, None)
        out.append(g)
    return out


def main(out):
    ents = []
    world = shell()
    ents.append(('worldspawn', [('classname', 'worldspawn'), ('editor_drLastCameraPos', '112 88 64'),
                                ('editor_drLastCameraAngle', '-10 0 0')], world))
    keys = []

    def static(name, model, origin, angle=0, extra=()):
        k = [('classname', 'func_static'), ('name', name), ('model', model), ('origin', '%g %g %g' % origin)]
        if angle:
            k.append(('angle', str(angle)))
        k += list(extra)
        ents.append((name, k, []))

    # where the head is: the menu camera / the player start for looking at the room in the game
    ents.append(('info_player_start_1', [('classname', 'info_player_start'), ('name', 'info_player_start_1'),
                                         ('origin', '112 88 1'), ('angle', '0')], []))

    # fireplace on the east wall, front to the west (the model's front is -y: angle 270), its parts as the
    # fireplace light holder would attach them (offsets turned with it)
    fp = (210, 88, 35.3)
    static('vr_menu_fireplace', 'models/darkmod/fireplace/fireplace_brick.ase', fp, 270)
    static('vr_menu_grate', 'models/darkmod/fireplace/grate.lwo', (fp[0], fp[1], fp[2] - 24), 270)
    static('vr_menu_logs', 'models/darkmod/fireplace/burntwood.lwo', (fp[0], fp[1], fp[2] - 24), 270, [('skin', 'burning_logs')])
    static('vr_menu_ash', 'models/darkmod/fireplace/ash_long.ase', (fp[0], fp[1], fp[2] - 30.5), 270)
    # the fire: light with its flame particle
    ents.append(('vr_menu_fire', [('classname', 'light'), ('name', 'vr_menu_fire'),
                                  ('origin', '%g %g %g' % (fp[0] + 3, fp[1], fp[2] - 21)),
                                  ('model', 'fireplace_flame_new01.prt'), ('texture', 'lights/fire_fireplace'),
                                  ('_color', '0.90 0.56 0.27'), ('light_center', '0 0 4'),
                                  ('light_radius', '260 260 220'), ('falloff', '0'), ('noshadows', '0')], []))
    ents.append(('vr_menu_fire_sound', [('classname', 'speaker'), ('name', 'vr_menu_fire_sound'),
                                        ('origin', '%g %g %g' % (fp[0], fp[1], 20)),
                                        ('s_shader', 'element_fire_fireplace'), ('s_looping', '1'),
                                        ('s_waitfortrigger', '0'), ('s_maxdistance', '40'), ('s_mindistance', '4')], []))

    # rug in front of the fire, long side north-south
    static('vr_menu_rug', 'models/darkmod/decorative/rug01.lwo', (150, 88, 0.1), 90)
    # the rocking chair before the fire (brushes, see rocking_chair)
    rc = (158, 90, 0)
    ents.append(('vr_menu_rocking_chair', [('classname', 'func_static'), ('name', 'vr_menu_rocking_chair'),
                                           ('model', 'vr_menu_rocking_chair'), ('origin', '0 0 0')],
                 [offset_faces(p, rc) for p in rocking_chair()]))
    # the stool beside it, a bottle on it
    static('vr_menu_stool', 'models/darkmod/furniture/seating/stool_round_rough.lwo', (172, 120, 0))
    static('vr_menu_bottle', 'models/darkmod/kitchen/bottle_simple_01.ase', (172, 120, 20.9))

    # the door on the west wall (frame across the wall: angle 90), the door closed inside the frame
    dy = 132
    static('vr_menu_doorframe', 'models/darkmod/architecture/doorframes/door_frame_wood_96x48.ase', (0, dy, 1.3), 90)
    static('vr_menu_door', 'models/darkmod/architecture/doors/door96x48_2hinge.lwo', (0, dy - 24, 0), 90)
    # the table next to the door, the chair in front of it (facing the table: west, angle 90)
    static('vr_menu_table', 'models/darkmod/furniture/tables/table_wood_rough_small.lwo', (32, 60, 0))
    static('vr_menu_chair', 'models/darkmod/furniture/seating/rusticchair.lwo', (72, 60, 19.9), 90)
    # a candle on the table
    static('vr_menu_candle', 'models/darkmod/lights/extinguishable/candle_wide.lwo', (28, 52, 36.7 + 7.0))
    ents.append(('vr_menu_candle_flame', [('classname', 'light'), ('name', 'vr_menu_candle_flame'),
                                          ('origin', '28 52 44.6'), ('model', 'tdm_fire_candle.prt'),
                                          ('texture', 'lights/biground_candleflicker_shadow'),
                                          ('_color', '0.55 0.38 0.18'), ('light_radius', '110 110 110')], []))

    # the bed along the south wall, headboard to the west
    static('vr_menu_bed', 'models/darkmod/furniture/beds/bed_single_timber.ase', (146, 34.5, 0), 90)

    # moonlight through the window, and a faint fill so nothing is pitch black
    ents.append(('vr_menu_moon', [('classname', 'light'), ('name', 'vr_menu_moon'), ('origin', '112 150 96'),
                                  ('texture', 'lights/biground1'), ('_color', '0.16 0.20 0.32'),
                                  ('light_radius', '170 150 150')], []))
    ents.append(('vr_menu_fill', [('classname', 'light'), ('name', 'vr_menu_fill'), ('origin', '112 88 80'),
                                  ('texture', 'lights/biground1'), ('_color', '0.07 0.06 0.06'),
                                  ('noshadows', '1'), ('light_radius', '260 220 160')], []))

    text = 'Version 2\n'
    for i, (_, keys, brushes) in enumerate(ents):
        text += entity(i, keys, brushes)
    with open(out, 'w') as f:
        f.write(text)
    print(out, len(ents), 'entities,', len(world), 'world brushes')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'vr_menu.map')
