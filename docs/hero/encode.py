"""Join the rendered turn into the README's animated WebP (transparent, looping).

    python3 encode.py <frames dir> <out.webp> [fps] [quality] [alpha quality]

The shadow's faintest edge (alpha under 4/255) is dropped: invisible, but it costs megabytes.
"""
import os, sys
import numpy as np
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
fps = float(sys.argv[3]) if len(sys.argv) > 3 else 20
quality = int(sys.argv[4]) if len(sys.argv) > 4 else 85
alpha_quality = int(sys.argv[5]) if len(sys.argv) > 5 else 70


def load(path):
    a = np.asarray(Image.open(path).convert('RGBA')).copy()
    clear = a[..., 3] < 4
    a[clear] = 0
    return Image.fromarray(a)


names = sorted(n for n in os.listdir(src) if n.endswith('.png') and n[:4].isdigit())
frames = [load(os.path.join(src, n)) for n in names]
frames[0].save(out, save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0,
               lossless=False, quality=quality, alpha_quality=alpha_quality, method=6)
print(f'{len(frames)} frames, {os.path.getsize(out) / 1e6:.2f} MB')
