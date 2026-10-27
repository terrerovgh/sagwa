import os, sys, numpy as np
from PIL import Image
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'sagwa', 'tools'))
import pack_clips as pc
SIZE = (480, 416)
SEQ = sys.argv[1].split(',')
out = sys.argv[2]; os.makedirs(out, exist_ok=True)
hubs = {n: pc.hub_frame(n, SIZE) for n in pc.HUB_IDS}
i = 0
for name in SEQ:
    a, b = pc.CLIP_HUBS[name]
    v = pc.read_video(os.path.join(pc.CLIPS, name + '.mp4'), SIZE)
    if a != b: v = pc.trim(v, False)
    v[0], v[-1] = hubs[a], hubs[b]
    E = 5
    for k in range(1, E):
        w = k / E
        v[k] = hubs[a] * (1 - w) + v[k] * w
        v[-1 - k] = hubs[b] * (1 - w) + v[-1 - k] * w
    for f in v[:-1]:   # drop last (== next clip's first hub frame)
        Image.fromarray(pc.clean(f)).save(f'{out}/{i:04d}.jpg', quality=92); i += 1
    print(name, len(v), f'{len(v)/24:.1f}s')
print('total', i, f'{i/24:.1f}s')
