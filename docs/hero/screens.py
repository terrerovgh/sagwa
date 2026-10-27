"""Put Sagwa's frames on the CoreS3's 4:3 screen: black 640x480 canvases, as the device shows them.

    python3 screens.py <frames dir from extract_frames.py> <out dir>
"""
import os, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
names = sorted(n for n in os.listdir(src) if n.endswith('.jpg'))
for n in names:
    canvas = Image.new('RGB', (640, 480))
    # 480x416 frame scaled to fill the height, feet just past the bottom edge, centred
    canvas.paste(Image.open(os.path.join(src, n)).convert('RGB').resize((552, 478), Image.LANCZOS), (44, 22))
    canvas.save(os.path.join(out, n), quality=95)
print(len(names), 'screens')
