#!/usr/bin/env python3
"""The VR menu room: a small old inn room (maps/vr_menu.map), laid out after the user's sketch.

Brushes for the shell (floor, walls with wainscot, ceiling with beams, a window), stock TDM models for the
furniture, a rocking chair built from brushes (TDM has no rocking chair model), lights and fire.

The map is meant to be drawn without the game running (the VR main menu draws its geometry, models, lights and
particles), so it uses only plain entities: func_static models, lights (a light's "model" is its flame particle),
a speaker. No fireplace light-holder entities with attachments: their parts are placed one by one.

Plain Python. Usage: build_menu_room.py <out.map>
Room interior: x 0..224, y 0..240, z 0..128. North (+y): fireplace, a bin in the corner; middle: rug with the
rocking chair and a stool facing the fire; east: table along the wall with two chairs, letters, a candle; west:
window; south-west: bed; south: chest; south-east (east wall): door. The camera is at the bed's corner by the
west wall, looking north-east over the room. Lit mostly by the fire, a little by the candle, least by the moon.
1 unit = 2.3 cm.
"""
import math
import sys

CAULK = 'textures/common/caulk'
FLOOR = 'textures/darkmod/wood/boards/worn_01'
WALL_LOW = 'textures/darkmod/wood/boards/dark_rough'
WALL_HIGH = 'textures/darkmod/wood/boards/rough_wood_brown_dull'
TRIM = 'textures/darkmod/wood/boards/timber_ends_dark'
CEILING = 'textures/darkmod/wood/boards/rough_wood_grey'
BEAM = 'textures/darkmod/wood/boards/rough_wood_darkbrown_dull'
WINDOW = 'textures/darkmod/window/wooden_frame01/wooden_frame01_moonlit'
CHAIR_WOOD = 'textures/darkmod/wood/boards/rough_wood_darkbrown_dull'

X0, X1, Y0, Y1, Z0, Z1 = 0, 224, 0, 240, 0, 128
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
    # the window on the west wall (a moonlit window texture over the wall face)
    wy0, wy1, wz0, wz1 = 118, 174, 46, 110
    corners = [(X0 + 0.5, wy0, wz0), (X0 + 0.5, wy1, wz0), (X0 + 0.5, wy0, wz1), (X0 + 0.5, wy1, wz1)]
    b.append(box((X0, wy0, wz0), (X0 + 0.5, wy1, wz1), {'+x': WINDOW}, fit={'+x': corners}))
    # ceiling beams (east-west) and timber posts in the corners and along the long walls
    for y in (60, 120, 180):
        b.append(box((X0, y - 5, Z1 - 9), (X1, y + 5, Z1), BEAM, tile=64.0))
    posts = [(X0, Y0), (X1 - 8, Y0), (X0, Y1 - 8), (X1 - 8, Y1 - 8), (X0, 88), (X0, 200), (X1 - 8, 200)]	# (not where the table stands)
    for x, y in posts:
        b.append(box((x, y, Z0), (x + 8, y + 8, Z1), BEAM, tile=64.0))
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


def offset_faces(faces, o, yaw=0.0):
    """turn (degrees about z) and move a brush"""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    out = []
    for f in faces:
        n = (f.n[0] * c - f.n[1] * s, f.n[0] * s + f.n[1] * c, f.n[2])
        out.append(Face(n, f.d + dot(n, o), f.mat, f.tile, None))
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

    def light(name, origin, keys):
        ents.append((name, [('classname', 'light'), ('name', name), ('origin', '%g %g %g' % origin)] + list(keys), []))

    # where the head is: at the bed's corner by the west wall, looking north-east over the room
    ents.append(('info_player_start_1', [('classname', 'info_player_start'), ('name', 'info_player_start_1'),
                                         ('origin', '12 80 1'), ('angle', '45')], []))

    # fireplace on the north wall, front to the south (the model's own front, -y), its parts placed as the fireplace
    # light holder would attach them
    fp = (99, 226, 35.3)
    static('vr_menu_fireplace', 'models/darkmod/fireplace/fireplace_brick.ase', fp)
    static('vr_menu_grate', 'models/darkmod/fireplace/grate.lwo', (fp[0], fp[1], fp[2] - 24))
    static('vr_menu_logs', 'models/darkmod/fireplace/burntwood.lwo', (fp[0], fp[1], fp[2] - 24), 0, [('skin', 'burning_logs')])
    static('vr_menu_ash', 'models/darkmod/fireplace/ash_long.ase', (fp[0], fp[1], fp[2] - 30.5))
    # the fire is the room's main light
    light('vr_menu_fire', (fp[0], fp[1] + 3, fp[2] - 21), [
        # the flame sits deep in the firebox, where the bricks shadow it: the light itself comes from the front
        # of the firebox (light_center), so it reaches the room
        ('model', 'fireplace_flame_new01.prt'), ('texture', 'lights/fire_fireplace'), ('_color', '1.0 0.62 0.30'),
        ('light_center', '0 -15 8'), ('light_radius', '300 300 240'), ('falloff', '0')])
    ents.append(('vr_menu_fire_sound', [('classname', 'speaker'), ('name', 'vr_menu_fire_sound'),
                                        ('origin', '%g %g %g' % (fp[0], fp[1] - 6, 20)),
                                        ('s_shader', 'element_fire_fireplace'), ('s_looping', '1'),
                                        ('s_waitfortrigger', '0'), ('s_maxdistance', '40'), ('s_mindistance', '4')], []))
    # a bin in the north-east corner
    static('vr_menu_bin', 'models/darkmod/containers/waste_basket_001.lwo', (204, 220, 0))

    # rug in the middle, the rocking chair on it facing the fire, a stool with a bottle beside it
    static('vr_menu_rug', 'models/darkmod/decorative/rug01.lwo', (93, 143, 0.1))
    ents.append(('vr_menu_rocking_chair', [('classname', 'func_static'), ('name', 'vr_menu_rocking_chair'),
                                           ('model', 'vr_menu_rocking_chair'), ('origin', '0 0 0')],
                 [offset_faces(p, (66, 166, 0.1), 90) for p in rocking_chair()]))
    static('vr_menu_stool', 'models/darkmod/furniture/seating/stool_round_rough.lwo', (98, 168, 0.1))
    static('vr_menu_bottle', 'models/darkmod/kitchen/bottle_simple_01.ase', (98, 168, 21))

    # the table along the east wall (long side north-south), a chair at its west side facing it and one at its
    # south end; letters, papers, a book, ink and a candle on it
    tz = 34.5
    static('vr_menu_table', 'models/darkmod/furniture/tables/table_tavern01.lwo', (197, 138, 0), 90)
    static('vr_menu_chair', 'models/darkmod/furniture/seating/rusticchair.lwo', (160, 143, 19.9), 270)
    static('vr_menu_chair2', 'models/darkmod/furniture/seating/rusticchair.lwo', (205, 76, 19.9), 0)
    static('vr_menu_letter1', 'models/darkmod/readables/letter_001.lwo', (190, 150, tz + 0.05), 20)
    static('vr_menu_letter2', 'models/darkmod/readables/letter_001.lwo', (197, 158, tz + 0.15), 345)
    static('vr_menu_letter3', 'models/darkmod/readables/letter_001.lwo', (186, 139, tz + 0.05), 75)
    static('vr_menu_paper1', 'models/darkmod/readables/paper1.lwo', (195, 170, tz + 0.05), 10)
    static('vr_menu_paper2', 'models/darkmod/readables/paper2.lwo', (189, 124, tz + 0.05), 290)
    static('vr_menu_papers', 'models/darkmod/readables/paper_stack_001.lwo', (206, 147, tz))
    static('vr_menu_book', 'models/darkmod/readables/book_open1_small.lwo', (200, 106, tz), 95)
    static('vr_menu_ink', 'models/darkmod/misc/ink_pot02_wquill.lwo', (210, 168, tz), 200)
    static('vr_menu_candle', 'models/darkmod/lights/extinguishable/candle_wide.lwo', (209, 124, tz + 7.0))
    light('vr_menu_candle_flame', (209, 124, tz + 7.9), [
        ('model', 'tdm_fire_candle.prt'), ('texture', 'lights/biground_candleflicker_shadow'),
        ('_color', '0.36 0.25 0.11'), ('light_radius', '100 100 90')])

    # the bed in the south-west corner (headboard to the west), a chest at the foot of the south wall
    static('vr_menu_bed', 'models/darkmod/furniture/beds/bed_single_timber.ase', (53, 35, 0), 90)
    static('vr_menu_chest', 'models/darkmod/containers/openable/chest_old_body.lwo', (145, 16, 0), 180)
    static('vr_menu_chest_lid', 'models/darkmod/containers/openable/chest_old_lid.lwo', (145, 1.7, 22), 180)

    # the door on the east wall, south end (frame across the wall), closed in its frame
    dy = 34
    static('vr_menu_doorframe', 'models/darkmod/architecture/doorframes/door_frame_wood_96x48.ase', (X1, dy, 1.3), 90)
    static('vr_menu_door', 'models/darkmod/architecture/doors/door96x48_2hinge.lwo', (X1, dy - 24, 0), 90)

    # a little moonlight through the window: the least of the three
    light('vr_menu_moon', (14, 146, 92), [('texture', 'lights/biground1'), ('_color', '0.045 0.055 0.09'),
                                         ('light_radius', '130 120 120')])

    text = 'Version 2\n'
    for i, (_, keys, brushes) in enumerate(ents):
        text += entity(i, keys, brushes)
    with open(out, 'w') as f:
        f.write(text)
    print(out, len(ents), 'entities,', len(world), 'world brushes')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'vr_menu.map')
