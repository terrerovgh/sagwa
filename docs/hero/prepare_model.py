"""Split M5Stack's official CoreS3 assembly into the parts the hero render uses.

    python3 prepare_model.py <CoreS3.stl> <out dir>

CoreS3.stl comes from m5stack/M5_Hardware (Products/K128_CoreS3/Structures). It holds the
middle frame, the DIN base, the base's DIN clip, an inner bar, the four mounting ears and an
exploded copy of everything. We keep the assembled frame, base and clip, drop the ears and the
copies, and measure the one part the file leaves out: the front glass, which fills the frame's rim.

Parts are written as PLY in Blender's axes, device upright and centred: +X right, -Y towards
the viewer (the screen), +Z up (the DIN clip on top, the camera edge at the bottom). Units: mm.
"""
import os, sys
import numpy as np, trimesh
from shapely.geometry import Polygon

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)

TOP, BACK = 11.2, -20.3                       # screen face and back plate, in the STL's y
CENTRE = (TOP + BACK) / 2
# STL (x, y up out of the screen, z towards the camera edge) -> Blender, centred
TO_BLENDER = np.array([[1, 0, 0, 0], [0, -1, 0, CENTRE], [0, 0, -1, 0], [0, 0, 0, 1]], float)

parts = trimesh.load(src, force='mesh').split(only_watertight=False)
named = {}
for p in parts:
    (x0, y0, z0), (x1, y1, z1) = p.bounds
    if x0 < -28 or x1 > 28:
        continue                                  # exploded copy
    if y1 > 11 and y0 < -4:
        named['frame'] = p
    elif y1 < -3 and y0 < -20 and x1 - x0 > 50:
        named['base'] = p
    elif z0 < -28:
        named['clip'] = p                         # DIN rail clip, on the base's back
    elif y0 > 5:
        named['bar'] = p                          # inside the frame, hidden under the glass
    # else: one of the four mounting ears, left out on purpose
assert set(named) == {'frame', 'base', 'clip', 'bar'}, named.keys()

# The glass sits on the frame's inner ledge (y 9.7) inside its rim, a hair below the rim's top.
sec = named['frame'].section(plane_origin=[0, 10.9, 0], plane_normal=[0, 1, 0])
loops = [Polygon(d[:, [0, 2]]) for d in sec.discrete]
hole = min((l for l in loops if l.contains(Polygon([(0, 0), (1, 0), (0, 1)]))), key=lambda l: l.area)
glass_poly = hole.buffer(-0.08).simplify(0.02)
half = (glass_poly.bounds[2] - glass_poly.bounds[0]) / 2
r = np.sqrt(((2 * half) ** 2 - glass_poly.area) / (4 - np.pi))
print(f'glass {2 * half:.2f} mm square, corner radius {r:.2f} mm')

for k, m in named.items():
    m = m.copy(); m.apply_transform(TO_BLENDER)
    m.export(os.path.join(out, k + '.ply'))
    print(k, len(m.faces), np.round(m.bounds, 2).tolist())
# The glass itself is built in Blender (a bevelled rounded square) from these numbers.
with open(os.path.join(out, 'glass.txt'), 'w') as f:
    f.write(f'{half:.4f} {r:.4f} {CENTRE - 11.0:.4f} {CENTRE - 9.72:.4f}\n')   # half size, radius, front y, back y
