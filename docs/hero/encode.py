"""Join the rendered turn into the README's animated WebP (transparent, looping).

    python3 encode.py <frames dir> <out.webp> [fps] [quality]
    python3 encode.py <still.png> <out.png>

The shadow pass leaves a faint veil (a few % alpha) over the whole invisible floor; lossy alpha
turns it into a grey band and steps. So the shadow's pixels (pure black, not fully opaque) lose
that veil and their noise, and the alpha is stored losslessly in fine steps of 4/255, too fine to
see but enough to keep the turn under 8 MB.
"""
import os, sys
import numpy as np
from PIL import Image, ImageFilter

src, out = sys.argv[1], sys.argv[2]
fps = float(sys.argv[3]) if len(sys.argv) > 3 else 20
quality = int(sys.argv[4]) if len(sys.argv) > 4 else 80
VEIL, STEP = 12, 4


def fade(h, w):
    """1 under the device, easing to 0 well inside the frame, so the floor never shows its edge."""
    y, x = np.mgrid[0:h, 0:w] / np.float32(w)
    r = np.sqrt(((x - 0.5) / 0.43) ** 2 + ((y - 0.83) / 0.17) ** 2)
    t = np.clip((1.0 - r) / 0.6, 0, 1)
    return t * t * (3 - 2 * t)


def load(path):
    a = np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)
    shadow = (a[..., :3].max(-1) < 4) & (a[..., 3] < 250)
    al = a[..., 3]
    soft = np.clip((al - VEIL) * 255 / (255 - VEIL), 0, 255) * fade(*al.shape)
    # the shadow pass's sample noise flickers frame to frame and costs bytes; blur it away
    soft = np.asarray(Image.fromarray(soft.astype(np.uint8)).filter(ImageFilter.GaussianBlur(4)), np.float32)
    al[shadow] = np.round(soft[shadow] / STEP) * STEP
    a[al < 1] = 0
    return Image.fromarray(np.round(a).astype(np.uint8))


if os.path.isfile(src):                 # a single still: same clean-up, saved as PNG
    load(src).save(out, optimize=True)
    print(f'{out}: {os.path.getsize(out) / 1e6:.2f} MB')
    sys.exit()

names = sorted(n for n in os.listdir(src) if n.endswith('.png') and n[:4].isdigit())
frames = [load(os.path.join(src, n)) for n in names]
frames[0].save(out, save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0,
               lossless=False, quality=quality, alpha_quality=100, method=4)
print(f'{len(frames)} frames, {os.path.getsize(out) / 1e6:.2f} MB')
