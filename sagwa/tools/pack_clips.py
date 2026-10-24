#!/usr/bin/env python3
"""Pack Sagwa's video clips into the frame pack the CoreS3 firmware plays.

    pack_clips.py [--out PATH] [--fps N] [--quality Q] [--preview DIR]

Every clip starts and ends on one of the shared hub poses (hubs/*.png, one
scale, one ground line), so clips chain without a dissolve. For each clip
this crops the same box out of the 1440 px video, drops the still lead-in
and tail, resamples to --fps, eases the first and last frames onto the exact
hub drawing, clamps the near-black background to pure black, and stores each
frame as a baseline JPEG for the ROM decoder.

The pack (little endian):
    header   "SGWC" u16 version, u16 width, u16 height, u16 fps,
             u16 clips, u16 reserved, u32 frames, u32 frame_table, u32 data
    clips    clips x { char name[20]; u16 first, count; u8 from, to, flags, pad }
    frames   frames x { u32 offset (from data), u32 size }
    data     the JPEGs
Hubs: 0 sleep, 1 loaf, 2 sit, 3 stand, 4 stalk. flags bit 0: loops on its hub.

Needs Pillow, numpy and imageio-ffmpeg.
"""
import argparse
import io
import os
import struct
import subprocess

import imageio_ffmpeg
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SAGWA = os.path.dirname(HERE)
CLIPS = os.path.join(SAGWA, 'clips')
HUBS = os.path.join(SAGWA, 'hubs')
DEFAULT_OUT = os.path.join(SAGWA, '..', 'muse-gadget-sdk', 'esp32', 'components', 'muse', 'avatar',
                           'sagwa_clips.bin')

HUB_IDS = {'sleep': 0, 'loaf': 1, 'sit': 2, 'stand': 3, 'stalk': 4}
HUB_FILES = {'sleep': 'sleep.png', 'loaf': 'wake.png', 'sit': 'listen.png', 'stand': 'idle.png',
             'stalk': 'work.png'}

# name: (from hub, to hub). A clip whose hubs match loops on that hub.
CLIP_HUBS = {
    'sleep_to_loaf': ('sleep', 'loaf'), 'loaf_to_sleep': ('loaf', 'sleep'),
    'loaf_to_sit': ('loaf', 'sit'), 'sit_to_loaf': ('sit', 'loaf'),
    'sit_to_stand': ('sit', 'stand'), 'stand_to_sit': ('stand', 'sit'),
    'loaf_to_stand': ('loaf', 'stand'),
    'stand_to_stalk': ('stand', 'stalk'), 'stalk_to_stand': ('stalk', 'stand'),
    'sit_idle': ('sit', 'sit'), 'sit_groom': ('sit', 'sit'), 'sit_meow': ('sit', 'sit'),
    'sit_think': ('sit', 'sit'), 'sit_startle': ('sit', 'sit'), 'sit_purr': ('sit', 'sit'),
    'loaf_idle': ('loaf', 'loaf'), 'loaf_listen': ('loaf', 'loaf'),
    'sleep_idle': ('sleep', 'sleep'),
    'stand_idle': ('stand', 'stand'), 'stand_walk': ('stand', 'stand'), 'stand_balance': ('stand', 'stand'),
    'stalk_idle': ('stalk', 'stalk'),
}

VIDEO_PX = 1440
HUB_PX = 1024                     # the hubs' canvas; the videos are it at 1440
CROP = (0, 120, 1440, 1360)       # one box for every clip, in video pixels
BLACK = 26                        # max channel below this is background
EASE_FRAMES = 3                   # frames eased onto the hub at each end
STILL = 1.2                       # mean abs change (0..255) that counts as moving


def read_video(path, size):
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    w, h = size
    x0, y0, x1, y1 = CROP
    vf = f'crop={x1 - x0}:{y1 - y0}:{x0}:{y0},scale={w}:{h}:flags=lanczos'
    out = subprocess.run([ff, '-loglevel', 'error', '-i', path, '-vf', vf, '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                          '-'], capture_output=True, check=True).stdout
    return np.frombuffer(out, np.uint8).reshape(-1, h, w, 3).astype(np.float32)


def hub_frame(name, size):
    """The hub drawing as a clip frame: the 1024 canvas at video scale, cropped."""
    im = Image.open(os.path.join(HUBS, HUB_FILES[name])).convert('RGB')
    im = im.resize((VIDEO_PX, VIDEO_PX), Image.LANCZOS).crop(CROP).resize(size, Image.LANCZOS)
    return np.asarray(im, np.float32)


def trim(frames, loop):
    """Drop the still lead-in and tail (a loop keeps a little of each)."""
    d = np.abs(np.diff(frames, axis=0)).mean(axis=(1, 2, 3))
    moving = np.nonzero(d > STILL)[0]
    if not len(moving):
        return frames
    keep = 6 if loop else 2
    a = max(0, moving[0] - keep)
    b = min(len(frames), moving[-1] + 2 + keep)
    return frames[a:b]


def clean(frame):
    f = np.clip(frame, 0, 255)
    bg = f.max(axis=2) < BLACK
    f[bg] = 0
    return f.astype(np.uint8)


def jpeg(frame, quality):
    buf = io.BytesIO()
    Image.fromarray(frame).save(buf, 'JPEG', quality=quality, subsampling=2, optimize=False, progressive=False)
    return buf.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--fps', type=int, default=12)
    ap.add_argument('--quality', type=int, default=80)
    ap.add_argument('--width', type=int, default=240)
    ap.add_argument('--height', type=int, default=208)
    ap.add_argument('--preview', help='write each clip as a contact sheet PNG here')
    args = ap.parse_args()
    size = (args.width, args.height)
    hubs = {n: hub_frame(n, size) for n in HUB_IDS}

    clips, frames = [], []
    for name, (a, b) in CLIP_HUBS.items():
        path = os.path.join(CLIPS, name + '.mp4')
        if not os.path.exists(path):
            print(f'{name}: missing, skipped')
            continue
        loop = a == b
        v = read_video(path, size)
        raw_first, raw_last = v[0].copy(), v[-1].copy()
        if not loop:
            v = trim(v, loop)
        step = max(1, round(24 / args.fps))
        v = v[::step]
        v[0], v[-1] = hubs[a], hubs[b]
        for k in range(1, EASE_FRAMES):
            w = k / EASE_FRAMES
            v[k] = hubs[a] * (1 - w) + v[k] * w
            v[-1 - k] = hubs[b] * (1 - w) + v[-1 - k] * w
        enc = [jpeg(clean(f), args.quality) for f in v]
        drift = (np.abs(raw_first - hubs[a]).mean(), np.abs(raw_last - hubs[b]).mean())
        clips.append((name, len(frames), len(enc), HUB_IDS[a], HUB_IDS[b], 1 if loop else 0))
        frames.extend(enc)
        print(f'{name:16s} {len(enc):3d} frames {len(enc) / args.fps:4.1f} s  avg {sum(map(len, enc)) / len(enc) / 1024:4.1f} KB'
              f'  hub drift {drift[0]:4.1f}/{drift[1]:4.1f}')
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            pick = v[:: max(1, len(v) // 12)][:12]
            sheet = np.concatenate([clean(f) for f in pick], axis=1)
            Image.fromarray(sheet).save(os.path.join(args.preview, name + '.png'))

    head = 28
    table = head + struct.calcsize('<20sHHBBBB') * len(clips)
    data = table + 8 * len(frames)
    out = bytearray(struct.pack('<4sHHHHHHIII', b'SGWC', 1, size[0], size[1], args.fps, len(clips), 0, len(frames),
                                table, data))
    assert len(out) == head
    for name, first, count, a, b, flags in clips:
        out += struct.pack('<20sHHBBBB', name.encode(), first, count, a, b, flags, 0)
    at = 0
    for f in frames:
        out += struct.pack('<II', at, len(f))
        at += len(f)
    for f in frames:
        out += f
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'wb') as fh:
        fh.write(out)
    print(f'{len(clips)} clips, {len(frames)} frames, {len(out) / 1048576:.2f} MB -> {os.path.abspath(args.out)}')


if __name__ == '__main__':
    main()
