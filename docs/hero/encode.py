"""Join the rendered turn into the README's animated WebP (transparent, looping).

    python3 encode.py <frames dir> <out.webp> [fps] [quality]
"""
import os, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
fps = float(sys.argv[3]) if len(sys.argv) > 3 else 20
quality = int(sys.argv[4]) if len(sys.argv) > 4 else 80
names = sorted(n for n in os.listdir(src) if n.endswith('.png') and n[:4].isdigit())
frames = [Image.open(os.path.join(src, n)).convert('RGBA') for n in names]
frames[0].save(out, save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0,
               lossless=False, quality=quality, alpha_quality=90, method=6, minimize_size=True)
print(f'{len(frames)} frames, {os.path.getsize(out) / 1e6:.2f} MB')
