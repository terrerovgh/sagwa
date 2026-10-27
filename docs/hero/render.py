"""Render the README's hero: M5Stack's CoreS3 with Sagwa on its screen, in Blender's Cycles.

    blender -b -P render.py -- <model dir> <screens dir> <out dir> <mode> [options]

model dir    the parts from prepare_model.py
screens dir  the 640x480 screens from screens.py (0000.jpg ...)
mode         sheet   six angles round the turn
             ref     the reference photo's three views (3/4, front, back)
             anim    the turn, frames --from..--to of --frames (one loop of the clips)
             still   one frame of the turn (--at, in turns)
             stl     the model, without the ears, as one STL lying screen up (Z up)
options      --res N  --samples N  --frames N  --from N  --to N  --at T  --fstop F  --shadow A

Units are mm (scene scale 0.001). Device axes: +X right, -Y the screen, +Z up (DIN clip on top,
the camera edge at the bottom). Everything visible that isn't in M5Stack's CAD is placed from
measurements of M5Stack's product photos, noted where it's built.
"""
import bpy, bmesh, math, os, sys
import numpy as np
from mathutils import Matrix, Vector

TAU = 2 * math.pi
rad = math.radians

argv = sys.argv[sys.argv.index('--') + 1:]
MODEL, SCREENS, OUT, MODE = argv[:4]
opt = {argv[i][2:]: argv[i + 1] for i in range(4, len(argv) - 1, 2)}
RES = int(opt.get('res', 1000))
SAMPLES = int(opt.get('samples', 128))
CLIP_FRAMES = len([n for n in os.listdir(SCREENS) if n.endswith('.jpg')])   # 428 at 24 fps
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.unit_settings.system = 'METRIC'
sc.unit_settings.scale_length = 0.001
sc.unit_settings.length_unit = 'MILLIMETERS'
CENTRE = -4.55          # prepare_model.py: device y (STL) -> Y = CENTRE - y

def Ydev(y_stl):
    return CENTRE - y_stl

# ---- materials ----------------------------------------------------------------

def principled(name, color, rough, metal=0.0, **kw):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*color, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    for k, v in kw.items():
        b.inputs[k].default_value = v
    return m

def textured_plastic(name, color, rough):
    """Matte moulded plastic: a fine spark-eroded grain in the bump and a hint of it in the gloss."""
    m = principled(name, color, rough)
    nt = m.node_tree; n = nt.nodes; L = nt.links
    b = n['Principled BSDF']
    tc = n.new('ShaderNodeTexCoord')
    noise = n.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = 9.0
    noise.inputs['Detail'].default_value = 6.0; noise.inputs['Roughness'].default_value = 0.65
    L.new(tc.outputs['Object'], noise.inputs['Vector'])
    bump = n.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.10
    bump.inputs['Distance'].default_value = 0.02
    L.new(noise.outputs['Fac'], bump.inputs['Height']); L.new(bump.outputs['Normal'], b.inputs['Normal'])
    ramp = n.new('ShaderNodeMapRange')
    ramp.inputs['To Min'].default_value = rough - 0.06; ramp.inputs['To Max'].default_value = rough + 0.06
    L.new(noise.outputs['Fac'], ramp.inputs['Value']); L.new(ramp.outputs['Result'], b.inputs['Roughness'])
    return m

# Shell: M5Stack's dark grey PC/ABS, matched against the product photos
PLASTIC = textured_plastic('plastic', (0.10, 0.10, 0.10), 0.5)
INSIDE = principled('inside', (0.006, 0.006, 0.007), 0.6)
STEEL = principled('steel', (0.78, 0.78, 0.80), 0.22, 1.0)
BRASS = principled('brass', (0.82, 0.56, 0.16), 0.24, 1.0)
COPPER = principled('copper', (0.93, 0.42, 0.18), 0.30, 1.0)
LENS = principled('lens', (0.03, 0.06, 0.11), 0.06, 0.4)
PUPIL = principled('pupil', (0.002, 0.002, 0.003), 0.05)
LED = principled('led', (0.02, 0.05, 0.04), 0.1, **{'Emission Color': (0.25, 1.0, 0.7, 1), 'Emission Strength': 0.6})
SENSOR = principled('sensor', (0.10, 0.10, 0.09), 0.15)
RED = principled('red', (0.55, 0.02, 0.02), 0.45)
RED_LABEL = principled('red_label', (0.60, 0.012, 0.015), 0.35)
WHITE_LABEL = principled('white_label', (0.80, 0.80, 0.78), 0.35)
INK_WHITE = principled('ink_white', (0.85, 0.85, 0.85), 0.4)
INK_BLACK = principled('ink_black', (0.01, 0.01, 0.01), 0.4)
BLACK_PLASTIC = principled('black_plastic', (0.012, 0.012, 0.013), 0.4)
CHROME = principled('nickel', (0.70, 0.70, 0.68), 0.18, 1.0)

# Screen glass: black, glossy, with the IPS image glowing from under it
SCREEN_W, SCREEN_H = 40.8, 30.6           # 2.0" 4:3 active area
SCREEN_X, SCREEN_Z = -0.1, -0.35          # its centre, from the front photo (11.07 px/mm)
screen_img = bpy.data.images.load(os.path.join(SCREENS, '0000.jpg'))

def glass_material():
    m = principled('glass', (0.004, 0.004, 0.005), 0.03, **{'IOR': 1.52})
    nt = m.node_tree; n = nt.nodes; L = nt.links
    b = n['Principled BSDF']
    tc = n.new('ShaderNodeTexCoord'); sep = n.new('ShaderNodeSeparateXYZ'); L.new(tc.outputs['Object'], sep.inputs[0])
    def lin(src, scale, offset):
        a = n.new('ShaderNodeMath'); a.operation = 'MULTIPLY_ADD'
        L.new(src, a.inputs[0]); a.inputs[1].default_value = scale; a.inputs[2].default_value = offset
        return a.outputs[0]
    u = lin(sep.outputs['X'], 1 / SCREEN_W, 0.5 - SCREEN_X / SCREEN_W)
    v = lin(sep.outputs['Z'], 1 / SCREEN_H, 0.5 - SCREEN_Z / SCREEN_H)
    comb = n.new('ShaderNodeCombineXYZ'); L.new(u, comb.inputs['X']); L.new(v, comb.inputs['Y'])
    tex = n.new('ShaderNodeTexImage'); tex.image = screen_img; tex.extension = 'CLIP'; tex.interpolation = 'Cubic'
    L.new(comb.outputs[0], tex.inputs['Vector'])
    # IPS black glows a little; the panel around it doesn't
    lift = n.new('ShaderNodeMix'); lift.data_type = 'RGBA'; lift.blend_type = 'ADD'
    lift.inputs['Factor'].default_value = 1.0
    L.new(tex.outputs['Color'], lift.inputs['A']); lift.inputs['B'].default_value = (0.010, 0.010, 0.012, 1)
    mask = n.new('ShaderNodeMix'); mask.data_type = 'RGBA'
    L.new(tex.outputs['Alpha'], mask.inputs['Factor']); mask.inputs['A'].default_value = (0, 0, 0, 1)
    L.new(lift.outputs['Result'], mask.inputs['B'])
    L.new(mask.outputs['Result'], b.inputs['Emission Color'])
    b.inputs['Emission Strength'].default_value = 1.0
    return m
GLASS = glass_material()

# ---- geometry helpers -----------------------------------------------------------

device = bpy.data.objects.new('device', None); sc.collection.objects.link(device)
parts = []

def add(name, me, mat, smooth_angle=None):
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o)
    o.parent = device
    if mat:
        me.materials.append(mat)
    if smooth_angle:
        for p in me.polygons:
            p.use_smooth = True
        me.set_sharp_from_angle(angle=math.radians(smooth_angle))
    parts.append(o)
    return o

def bm_to_mesh(name, bm):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); return me

def box(name, x0, x1, y0, y1, z0, z1, mat, bevel=0.0):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((x0 if v.co.x < 0 else x1, y0 if v.co.y < 0 else y1, z0 if v.co.z < 0 else z1))
    if bevel:
        bmesh.ops.bevel(bm, geom=bm.edges[:], offset=bevel, segments=2, affect='EDGES')
    return add(name, bm_to_mesh(name, bm), mat, 40 if bevel else None)

def rounded_prism(name, hw, hh, r, y0, y1, mat, front_bevel=0.0, seg=16, at=(0.0, 0.0)):
    """Rounded rectangle in X/Z (half sizes hw, hh, corner radius r), from y0 to y1 along Y."""
    pts = []
    for cx, cz, a0 in ((hw - r, hh - r, 0), (-hw + r, hh - r, 90), (-hw + r, -hh + r, 180), (hw - r, -hh + r, 270)):
        for i in range(seg + 1):
            a = math.radians(a0 + 90 * i / seg)
            pts.append((at[0] + cx + r * math.cos(a), at[1] + cz + r * math.sin(a)))
    bm = bmesh.new()
    front = bm.faces.new([bm.verts.new((x, y0, z)) for x, z in pts])
    back = bm.faces.new([bm.verts.new((x, y1, z)) for x, z in reversed(pts)])
    n = len(pts)
    fv, bv = front.verts[:], list(reversed(back.verts[:]))
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([fv[j], fv[i], bv[i], bv[j]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    if front_bevel:
        bmesh.ops.bevel(bm, geom=front.edges[:], offset=front_bevel, segments=3, affect='EDGES')
    return add(name, bm_to_mesh(name, bm), mat, 35)

def cylinder(name, r, length, mat, loc, axis='Y', seg=48, r2=None, bevel=0.0):
    """Cylinder (or cone, with r2) starting at loc and running `length` along +axis."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r if r2 is None else r2, depth=length)
    bmesh.ops.translate(bm, verts=bm.verts[:], vec=(0, 0, length / 2))
    if bevel:
        top = [e for e in bm.edges if all(v.co.z > length - 1e-4 for v in e.verts)]
        bmesh.ops.bevel(bm, geom=top, offset=bevel, segments=4, affect='EDGES')
    rot = {'Y': Matrix.Rotation(math.radians(-90), 4, 'X'), 'X': Matrix.Rotation(math.radians(90), 4, 'Y'),
           'Z': Matrix.Identity(4), '-Y': Matrix.Rotation(math.radians(90), 4, 'X'),
           '-X': Matrix.Rotation(math.radians(-90), 4, 'Y')}[axis]
    bmesh.ops.transform(bm, matrix=Matrix.Translation(loc) @ rot, verts=bm.verts[:])
    return add(name, bm_to_mesh(name, bm), mat, 30)

def rr_loop(hw, hh, r, seg=12):
    """Rounded rectangle outline (half sizes hw, hh, corner radius r) as 2D points."""
    pts = []
    for cx, cz, a0 in ((hw - r, hh - r, 0), (-hw + r, hh - r, 90), (-hw + r, -hh + r, 180), (hw - r, -hh + r, 270)):
        for i in range(seg + 1):
            a = math.radians(a0 + 90 * i / seg)
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))
    return pts

def circle_loop(r, seg=64, star=0.0):
    """Circle outline; star > 0 alternates the radius (a knurled flank)."""
    return [((r - star * (i % 2)) * math.cos(TAU * i / seg), (r - star * (i % 2)) * math.sin(TAU * i / seg))
            for i in range(seg)]

def loop_prism(name, outer, inner, t0, t1, mat, to3d):
    """Prism between depths t0 and t1 (t0 = the visible face) of an outline, hollow if inner given.
    to3d(a, b, t) maps outline coordinates and depth to device coordinates."""
    bm = bmesh.new()
    def ring_verts(pts, t):
        return [bm.verts.new(to3d(a, b, t)) for a, b in pts]
    o0, o1 = ring_verts(outer, t0), ring_verts(outer, t1)
    n = len(outer)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([o0[i], o0[j], o1[j], o1[i]])
    if inner:
        i0, i1 = ring_verts(inner, t0), ring_verts(inner, t1)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new([i0[j], i0[i], i1[i], i1[j]])
            bm.faces.new([o0[j], o0[i], i0[i], i0[j]])
            bm.faces.new([o1[i], o1[j], i1[j], i1[i]])
    else:
        bm.faces.new(o0); bm.faces.new(list(reversed(o1)))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return add(name, bm_to_mesh(name, bm), mat, 35)

def on_front(x, z):
    """Outline on the glass, at depth t towards the inside (+Y) from the glass face."""
    return lambda a, b, t: (x + a, GLASS_Y + t, z + b)

def on_back(x, z):
    """Outline on the back plate, depth t inwards (-Y) from the plate."""
    return lambda a, b, t: (x + a, PLATE - t, z + b)

def on_left(y, z):
    """Outline on the left side (x = -27); a runs along Y, b along Z, t inwards (+X) from x = -27."""
    return lambda a, b, t: (-27 + t, y + a, z + b)

def subtract(obj, cutter):
    mod = obj.modifiers.new('cut', 'BOOLEAN'); mod.object = cutter; mod.operation = 'DIFFERENCE'
    mod.solver = 'EXACT'
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    obj.modifiers.clear(); obj.data = me
    bpy.data.objects.remove(cutter); parts.remove(cutter) if cutter in parts else None

def text(name, body, size, mat, matrix, align='CENTER'):
    cu = bpy.data.curves.new(name, 'FONT'); cu.body = body; cu.size = size
    cu.align_x = align; cu.align_y = 'CENTER'; cu.extrude = 0.01
    font = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
    if os.path.exists(font):
        cu.font = bpy.data.fonts.load(font)
    o = bpy.data.objects.new(name, cu); sc.collection.objects.link(o)
    o.parent = device; o.matrix_parent_inverse = Matrix.Identity(4); o.matrix_basis = matrix
    o.data.materials.append(mat); parts.append(o)
    return o

# ---- M5Stack's CAD: middle frame, DIN base and its clip ---------------------------

for name in ('frame', 'base', 'clip'):
    bpy.ops.wm.ply_import(filepath=os.path.join(MODEL, name + '.ply'))
    o = bpy.context.selected_objects[0]; o.name = name
    o.data.materials.clear(); o.data.materials.append(PLASTIC)
    o.parent = device; parts.append(o)
    for p in o.data.polygons:
        p.use_smooth = True
    o.data.set_sharp_from_angle(angle=math.radians(28))

# Interior: what you see through the ports and holes is the dark PCB stack
box('inside', -24.9, 24.9, -14.1, 11.4, -24.9, 24.9, INSIDE)

# ---- Front glass (the CAD leaves it out): fills the frame's rim -------------------

g_half, g_r, gy0, gy1 = map(float, open(os.path.join(MODEL, 'glass.txt')).read().split())
rounded_prism('glass', g_half, g_half, g_r, gy0, gy1, GLASS, front_bevel=0.12)

# Camera with its copper ring, mics and proximity sensor; centres from the front photo
GLASS_Y = gy0
loop_prism('cam_ring', circle_loop(1.85), circle_loop(1.42), -0.12, 0.02, COPPER, on_front(-0.1, -21.3))
loop_prism('cam_lens', circle_loop(1.42), None, -0.03, 0.02, LENS, on_front(-0.1, -21.3))
loop_prism('cam_pupil', circle_loop(0.55, 32), None, -0.04, 0.0, PUPIL, on_front(-0.1, -21.3))
for i, (x, z) in enumerate(((-14.2, -20.6), (13.9, -20.6))):        # microphones
    loop_prism(f'mic_ring{i}', circle_loop(0.62, 32), circle_loop(0.48, 32), -0.01, 0.01, SENSOR, on_front(x, z))
    loop_prism(f'mic{i}', circle_loop(0.48, 32), None, -0.008, 0.01, PUPIL, on_front(x, z))
loop_prism('prox0', circle_loop(0.42, 32), None, -0.01, 0.01, SENSOR, on_front(6.5, -21.3))
loop_prism('prox1', circle_loop(0.42, 32), None, -0.01, 0.01, LED, on_front(8.85, -21.3))

# ---- Ports, from the CAD's openings ----------------------------------------------

# Left side (x = -27): Port A (red HY2.0-4P) and the USB-C receptacle
pz0, pz1, py0, py1 = -18.9, -9.1, Ydev(4.6), Ydev(-0.4)
for nm, (z0, z1, y0, y1) in {'top': (pz0, pz1, py0, py0 + 0.75), 'bot': (pz0, pz1, py1 - 0.75, py1),
                             'l': (pz0, pz0 + 0.75, py0, py1), 'r': (pz1 - 0.75, pz1, py0, py1)}.items():
    box('portA_' + nm, -26.4, -24.6, y0, y1, z0, z1, RED, bevel=0.05)
box('portA_key', -26.0, -24.6, py0 + 0.75, py0 + 1.3, -15.2, -12.8, RED)
uy = (Ydev(1.5) + Ydev(4.6)) / 2
loop_prism('usbc_shell', rr_loop(1.43, 4.3, 1.42), rr_loop(1.2, 4.07, 1.19), 0.65, 2.4, CHROME, on_left(uy, 0))
loop_prism('usbc_back', rr_loop(1.2, 4.07, 1.19), None, 2.3, 2.4, BLACK_PLASTIC, on_left(uy, 0))
box('usbc_tongue', -26.0, -24.7, uy - 0.33, uy + 0.33, -3.3, 3.3, BLACK_PLASTIC, bevel=0.05)
for i in range(6):
    box(f'usbc_pin{i}', -25.9, -24.7, uy - 0.335, uy + 0.335, -2.75 + i * 1.1, -2.45 + i * 1.1, BRASS)

# Bottom (camera edge, z = -27): DC jack, DC INPUT label, power switch with its OFF/ON label
box('dc_body', -14.6, -5.4, Ydev(-4.6), Ydev(-11.1), -26.0, -20.0, BLACK_PLASTIC, bevel=0.2)
cylinder('dc_hole', 2.9, 1.0, INSIDE, Vector((-10.0, Ydev(-7.85), -26.05)), axis='Z')
cylinder('dc_pin', 0.65, 2.0, CHROME, Vector((-10.0, Ydev(-7.85), -26.2)), axis='Z')
FACE = Matrix.Rotation(math.radians(180), 4, 'X')          # text on the bottom face, read from below
def bottom(x, y, z):
    return Matrix.Translation((x, y, z)) @ FACE
box('dc_label_red', -4.85, 3.95, Ydev(-5.25), Ydev(-8.25), -26.84, -26.6, RED_LABEL)
box('dc_label_white', -4.85, 3.95, Ydev(-8.25), Ydev(-11.25), -26.84, -26.6, WHITE_LABEL)
text('dc_text', 'DC INPUT', 1.35, INK_WHITE, bottom(-0.45, Ydev(-6.75), -26.85))
text('dc_volts', '9~24V', 1.75, INK_BLACK, bottom(-0.45, Ydev(-9.75), -26.85))
sy0, sy1 = Ydev(-4.5), Ydev(-10.4)
box('sw_label_a', 5.7, 14.3, sy0, Ydev(-6.1), -26.05, -25.8, RED_LABEL)
box('sw_label_b', 5.7, 14.3, Ydev(-8.3), sy1, -26.05, -25.8, RED_LABEL)
box('sw_label_c', 5.7, 7.9, Ydev(-6.1), Ydev(-8.3), -26.05, -25.8, RED_LABEL)
box('sw_label_d', 12.1, 14.3, Ydev(-6.1), Ydev(-8.3), -26.05, -25.8, RED_LABEL)
text('sw_off', 'OFF', 1.15, INK_WHITE, bottom(8.1, Ydev(-5.3), -26.06), 'CENTER')
text('sw_on', 'ON', 1.15, INK_WHITE, bottom(12.0, Ydev(-9.45), -26.06), 'CENTER')
box('sw_knob', 8.0, 9.9, Ydev(-6.25), Ydev(-8.15), -26.4, -25.0, BLACK_PLASTIC, bevel=0.15)

# ---- Back: the DIN base's screws, brass inserts and centre screw -------------------

PLATE = Ydev(-17.3)                 # the back plate's face; the screws sit in 3 mm counterbores
for i, (x, z) in enumerate(((-22, -22), (22, -22), (-22, 22), (22, 22))):
    head = cylinder(f'screw{i}', 2.45, 1.45, STEEL, Vector((x, PLATE, z)), axis='Y', bevel=0.55, seg=64)
    sock = cylinder(f'socket{i}', 1.15, 2.0, STEEL, Vector((x, PLATE + 0.75, z)), axis='Y', seg=6)
    subtract(head, sock)
for i, x in enumerate((-12, 12)):
    # knurled brass insert; its top course fills the hole's 1 mm chamfer (about 5 mm across, as photographed)
    loop_prism(f'insert{i}', circle_loop(1.98, 48, star=0.16), circle_loop(1.12, 48), 0.9, 3.0, BRASS, on_back(x, 0))
    loop_prism(f'insert_top{i}', circle_loop(2.45, 56, star=0.18), circle_loop(1.12, 56), -0.02, 0.9, BRASS, on_back(x, 0))
# centre: a low translucent-grey boss with a small screw, as on the photographed base
BOSS = principled('boss', (0.16, 0.16, 0.16), 0.32)
cylinder('centre_boss', 2.5, 0.35, BOSS, Vector((0, PLATE - 0.05, 0)), axis='Y', bevel=0.25, seg=64)
cylinder('centre_screw', 0.65, 0.2, STEEL, Vector((0, PLATE + 0.25, 0)), axis='Y', bevel=0.12)
cylinder('centre_cross', 0.3, 0.2, INSIDE, Vector((0, PLATE + 0.3, 0)), axis='Y', seg=4)

# ---- Light, camera, world ------------------------------------------------------------

world = bpy.data.worlds.new('studio'); sc.world = world; world.use_nodes = True
wn = world.node_tree.nodes; wl = world.node_tree.links
env = wn.new('ShaderNodeTexEnvironment')
env.image = bpy.data.images.load(os.path.join(os.path.dirname(bpy.app.binary_path), '4.2', 'datafiles',
                                              'studiolights', 'world', 'studio.exr'))
mp = wn.new('ShaderNodeMapping'); tcw = wn.new('ShaderNodeTexCoord')
mp.inputs['Rotation'].default_value = (0, 0, math.radians(200))
wl.new(tcw.outputs['Generated'], mp.inputs['Vector']); wl.new(mp.outputs['Vector'], env.inputs['Vector'])
neutral = wn.new('ShaderNodeHueSaturation'); neutral.inputs['Saturation'].default_value = 0.2   # a grey studio
wl.new(env.outputs['Color'], neutral.inputs['Color'])
wl.new(neutral.outputs['Color'], wn['Background'].inputs['Color'])
wn['Background'].inputs['Strength'].default_value = 0.7
# The glass and metals mirror the studio only faintly, so the black glass stays black
lp = wn.new('ShaderNodeLightPath'); dim = wn.new('ShaderNodeMath'); dim.operation = 'MULTIPLY_ADD'
wl.new(lp.outputs['Is Glossy Ray'], dim.inputs[0]); dim.inputs[1].default_value = -0.75; dim.inputs[2].default_value = 1.0
mul = wn.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'; mul.inputs[1].default_value = 0.7
wl.new(dim.outputs[0], mul.inputs[0]); wl.new(mul.outputs[0], wn['Background'].inputs['Strength'])

def area(name, size, power, loc, target=(0, 0, 0), shape='RECTANGLE', size_y=None, glossy=True):
    li = bpy.data.lights.new(name, 'AREA'); li.shape = shape; li.size = size; li.size_y = size_y or size
    li.energy = power
    o = bpy.data.objects.new(name, li); sc.collection.objects.link(o); o.location = loc
    o.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    o.visible_glossy = glossy
    return o

# Key softbox high on the left, a broad strip behind the camera that the glass mirrors,
# a rim from behind on the right and a fill low on the right
area('key', 260, 7.5e5, (-260, -230, 300), size_y=180)
area('strip', 520, 1.6e5, (60, -480, 340), size_y=55)
area('rim', 200, 4.6e5, (300, 260, 160), size_y=120)
area('fill', 300, 1.9e5, (320, -260, 20), size_y=200, glossy=False)   # unseen in the glass

cam = bpy.data.objects.new('camera', bpy.data.cameras.new('camera')); sc.collection.objects.link(cam)
sc.camera = cam
cam.data.lens = 85; cam.data.sensor_width = 36
cam.data.clip_start = 10; cam.data.clip_end = 5000
cam.data.dof.use_dof = True
cam.data.dof.aperture_fstop = float(opt.get('fstop', 11))

def aim(distance, elevation, azimuth=0.0, target=(0, 0, -1.5), lens=85):
    e, a = math.radians(elevation), math.radians(azimuth)
    t = Vector(target)
    cam.location = t + distance * Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e)))
    cam.rotation_euler = (t - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = lens
    cam.data.dof.focus_distance = (t - cam.location).length - 6.0     # focus a little ahead of centre

# Soft contact shadow, rendered on its own (an HDRI on a shadow catcher leaves streaks): the floor
# lit by an even sky and one big overhead box, the device unseen but casting, then laid under it.
FLOOR_Z = -27.0 - 9.0
bpy.ops.mesh.primitive_plane_add(size=420, location=(0, 0, FLOOR_Z))
floor = bpy.context.object; floor.name = 'floor'; floor.is_shadow_catcher = True
top = area('top', 380, 3.0e5, (0, -30, 380), target=(0, 0, FLOOR_Z), glossy=False)
SHADOW = float(opt.get('shadow', 0.5))          # opacity of the shadow

# ---- Render settings ------------------------------------------------------------------

sc.render.engine = 'CYCLES'
cy = sc.cycles
cy.device = 'CPU'; cy.samples = SAMPLES; cy.use_adaptive_sampling = True; cy.adaptive_threshold = 0.015
cy.use_denoising = True; cy.denoiser = 'OPENIMAGEDENOISE'
cy.max_bounces = 8; cy.glossy_bounces = 4; cy.diffuse_bounces = 3; cy.transmission_bounces = 2
cy.caustics_reflective = False; cy.caustics_refractive = False
sc.render.film_transparent = True
sc.render.resolution_x = sc.render.resolution_y = RES
sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
sc.render.image_settings.color_depth = '8'
sc.view_settings.view_transform = 'Standard'; sc.view_settings.look = 'None'
sc.view_settings.exposure = float(opt.get('exposure', 0.0))
sc.render.use_persistent_data = True


def place(R, lift=0.0):
    device.matrix_world = Matrix.Translation((0, 0, lift)) @ R

def turn_pose(p):
    """One loop: a full turn that lingers in front and sweeps past the back, floating gently."""
    yaw = TAU * p - 0.62 * math.sin(TAU * p) + rad(22)
    pitch = rad(-7) + rad(3) * math.sin(TAU * 2 * p + 0.5)
    roll = rad(1.5) * math.sin(TAU * p)
    R = Matrix.Rotation(pitch, 4, 'X') @ Matrix.Rotation(yaw, 4, 'Z') @ Matrix.Rotation(roll, 4, 'Y')
    return R, 1.1 * math.sin(TAU * 2 * p)

def set_screen(k):
    screen_img.filepath = os.path.join(SCREENS, f'{k % CLIP_FRAMES:04d}.jpg')
    screen_img.reload()

def shoot(path, shadow=True):
    """Device pass (HDRI and studio lights), then, if wanted, the shadow pass laid under it."""
    sc.render.filepath = path
    floor.hide_render, top.hide_render = True, True
    bpy.ops.render.render(write_still=True)
    if not shadow:
        return
    keep = {o: (o.visible_camera, o.hide_render) for o in parts}
    lights = [o for o in sc.objects if o.type == 'LIGHT' and o is not top]
    for o in parts:
        o.visible_camera = False
    for o in lights:
        o.hide_render = True
    floor.hide_render, top.hide_render = False, False
    bg = wn['Background']
    world.node_tree.links.remove(bg.inputs['Color'].links[0]); world.node_tree.links.remove(bg.inputs['Strength'].links[0])
    strength = bg.inputs['Strength'].default_value
    bg.inputs['Color'].default_value = (1, 1, 1, 1); bg.inputs['Strength'].default_value = 1.0
    samples = cy.samples; cy.samples = 16
    sc.render.filepath = path + '.shadow.png'
    bpy.ops.render.render(write_still=True)
    cy.samples = samples
    bg.inputs['Strength'].default_value = strength
    world.node_tree.links.new(neutral.outputs['Color'], bg.inputs['Color']); world.node_tree.links.new(mul.outputs[0], bg.inputs['Strength'])
    for o, (cam_vis, hide) in keep.items():
        o.visible_camera = cam_vis; o.hide_render = hide
    for o in lights:
        o.hide_render = False
    # device over the shadow (premultiplied over), the shadow black with its alpha scaled
    def load(p):
        im = bpy.data.images.load(p); a = np.empty(len(im.pixels), np.float32); im.pixels.foreach_get(a)
        w, h = im.size; bpy.data.images.remove(im); return a.reshape(h, w, 4)
    dev, sh = load(path), load(path + '.shadow.png')
    sa = sh[..., 3:] * SHADOW
    out = dev[..., :3] * dev[..., 3:]                           # premultiply
    alpha = dev[..., 3:] + sa * (1 - dev[..., 3:])
    rgb = np.where(alpha > 1e-6, out / np.maximum(alpha, 1e-6), 0)
    im = bpy.data.images.new('out', dev.shape[1], dev.shape[0], alpha=True)
    im.pixels.foreach_set(np.concatenate([rgb, alpha], -1).ravel())
    im.filepath_raw = path; im.file_format = 'PNG'; im.save(); bpy.data.images.remove(im)
    os.remove(path + '.shadow.png')

def turn_frame(i, n):
    """Output frame i of n: the turn and the clips share one loop, so it closes seamlessly."""
    p = i / n
    R, lift = turn_pose(p)
    place(R, lift)
    set_screen(math.floor(p * CLIP_FRAMES + 1e-6))

aim(250, 9)
if MODE == 'sheet':
    set_screen(0)
    for k, (yaw, label) in enumerate(((22, '3q'), (0, 'front'), (-62, 'right'), (75, 'left'), (180, 'back'), (140, 'back3q'))):
        place(Matrix.Rotation(rad(-7), 4, 'X') @ Matrix.Rotation(rad(yaw), 4, 'Z'))
        shoot(os.path.join(OUT, f'{k}_{label}.png'))
elif MODE == 'ref':
    set_screen(0)
    # the photo's 3/4: lying screen up, seen from above with the left side and the camera edge
    place(Matrix.Rotation(rad(float(opt.get('ryaw', 35))), 4, 'Z') @ Matrix.Rotation(rad(-90), 4, 'X'))
    aim(330, float(opt.get('relev', 50)), target=(0, 0, 0), lens=125)
    shoot(os.path.join(OUT, 'ref_3q.png'), shadow=False)
    place(Matrix.Identity(4))
    aim(900, 0, target=(0, 0, 0), lens=380); cam.data.dof.use_dof = False
    shoot(os.path.join(OUT, 'ref_front.png'), shadow=False)
    place(Matrix.Rotation(rad(180), 4, 'Z'))
    shoot(os.path.join(OUT, 'ref_back.png'), shadow=False)
elif MODE == 'still':
    p = float(opt.get('at', 0))
    R, lift = turn_pose(p); place(R, lift); set_screen(math.floor(p * CLIP_FRAMES + 1e-6))
    shoot(os.path.join(OUT, 'still.png'))
elif MODE == 'anim':
    n = int(opt.get('frames', CLIP_FRAMES))
    for i in range(int(opt.get('from', 0)), int(opt.get('to', n))):
        path = os.path.join(OUT, f'{i:04d}.png')
        if os.path.exists(path):
            continue
        turn_frame(i, n)
        shoot(path)
elif MODE == 'stl':
    place(Matrix.Rotation(rad(-90), 4, 'X'))          # lying screen up, Z up
    keep = [o for o in parts if o.type == 'MESH' and o.name != 'inside']
    for o in sc.objects:
        o.select_set(o in keep)
    bpy.ops.wm.stl_export(filepath=os.path.join(OUT, 'cores3.stl'), export_selected_objects=True,
                          apply_modifiers=True, ascii_format=False, global_scale=1.0, use_scene_unit=False,
                          forward_axis='Y', up_axis='Z')
