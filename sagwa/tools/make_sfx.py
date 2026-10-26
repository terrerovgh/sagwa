#!/usr/bin/env python3
"""Build Sagwa's feline sound bank (sagwa_sfx_data.h) from the recordings.

    make_sfx.py [--out PATH] [--wav DIR]

Every sound Sagwa makes is cut from a real cat recording in sounds/cc0/
(CC0 / public domain, Wikimedia Commons, see ATTRIBUTION.md) or kept from
the older bank in sounds/legacy/ (16 kHz s16le). Each kind of sound has a few
takes so it is recognisable without being the same clip every time; the
firmware picks one at random and nudges its pitch a little.

For each take this decodes the recording to 16 kHz mono, cuts the given
spans, shifts the pitch tape-style (resampling, so higher is also shorter),
takes out rumble the 1 W speaker can't play, normalises to the kind's level
and fades the ends. --wav writes every take as a .wav to listen to.

Needs numpy and imageio-ffmpeg (the same as pack_clips.py).
"""
import argparse
import os
import subprocess
import wave

import imageio_ffmpeg
import numpy as np

RATE = 16000
HERE = os.path.dirname(os.path.abspath(__file__))
SOUNDS = os.path.join(HERE, "..", "sounds")
OUT = os.path.join(HERE, "..", "..", "muse-gadget-sdk", "esp32", "components", "muse", "sagwa_sfx_data.h")

# A take: a list of pieces played one after another. A piece is
# (file, start s, end s, pitch factor, gain) or ("gap", seconds).
# Kind: (C name, level as peak 0..1, takes, what it is for)
KINDS = [
    ("URGENT", 0.95, [
        [("pleading-cat.oga", 3.76, 4.66, 1.00, 1.0), ("gap", 0.12), ("pleading-cat.oga", 6.75, 7.45, 1.04, 0.85)],
        [("siamese-meow.wav", 0.27, 1.02, 1.00, 1.0), ("gap", 0.10), ("siamese-meow.wav", 0.27, 1.02, 1.08, 0.75)],
        [("female-meow.ogg", 0.02, 1.00, 1.00, 1.0), ("gap", 0.10), ("pleading-cat.oga", 9.46, 9.97, 1.05, 0.8)],
    ], "Insistent double meow: something important, calling you"),
    ("NOTIFY", 0.75, [
        [("greek-touch-mrrp.flac", 0.26, 0.84, 1.15, 1.0)],
        [("black-cat-begging.flac", 3.47, 3.84, 1.30, 1.0)],
        [("female-meow.ogg", 1.27, 1.55, 1.10, 1.0), ("gap", 0.06), ("female-meow.ogg", 1.27, 1.55, 1.22, 0.8)],
    ], "Short bright chirp: a new message, nothing urgent"),
    ("CHAT", 0.55, [
        [("black-cat-begging.flac", 0.95, 1.37, 1.05, 1.0)],
        [("black-cat-begging.flac", 4.88, 5.20, 1.10, 1.0)],
        [("black-cat-begging.flac", 2.37, 2.80, 1.00, 1.0)],
    ], "Soft low mrrow: talking with you, a reply in the conversation"),
    ("YAWN", 0.45, [
        [("pleading-cat.oga", 0.32, 0.92, 0.72, 1.0)],
    ], "Slow low yawn when she lies down to sleep or stretches"),
    ("STARTLE", 0.70, [
        [("young-female-meow.ogg", 0.02, 0.42, 1.10, 1.0)],
        [("hiss.wav", 1.41, 2.00, 1.00, 1.0)],
    ], "Surprised mew or a little hiss when startled"),
    ("HUNT", 0.45, [
        [("greek-touch-mrrp.flac", 0.30, 0.36, 1.35, 1.0), ("gap", 0.05)] * 3
        + [("greek-touch-mrrp.flac", 0.30, 0.36, 1.45, 1.0), ("gap", 0.05)] * 3
        + [("greek-touch-mrrp.flac", 0.30, 0.40, 1.25, 1.0)],
    ], "Chattering 'ek-ek-ek' while she stalks"),
    ("HAPPY", 0.75, [
        [("greek-touch-mrrp.flac", 0.26, 0.84, 1.25, 1.0), ("gap", 0.04), ("black-cat-begging.flac", 0.07, 0.54, 1.20, 0.9)],
        [("legacy:trill_listen.pcm", None, None, 1.10, 1.0), ("gap", 0.05), ("greek-touch-mrrp.flac", 0.26, 0.84, 1.30, 0.9)],
    ], "Happy trill: hello, or the update went well"),
    ("TRILL_LISTEN", 0.75, [
        [("legacy:trill_listen.pcm", None, None, 1.00, 1.0)],
    ], "Rising trill after a voice note goes"),
    ("MURMUR", 0.55, [
        [("legacy:murmur_activity.pcm", None, None, 1.00, 1.0)],
        [("black-cat-begging.flac", 0.95, 1.37, 0.92, 1.0)],
    ], "Throat mrrp while thinking or prowling"),
]

_cache = {}


def load(name):
    if name not in _cache:
        if name.startswith("legacy:"):
            raw = open(os.path.join(SOUNDS, "legacy", name[7:]), "rb").read()
        else:
            raw = subprocess.run(
                [imageio_ffmpeg.get_ffmpeg_exe(), "-v", "error", "-i", os.path.join(SOUNDS, "cc0", name),
                 "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
                capture_output=True, check=True).stdout
        _cache[name] = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    return _cache[name]


def fade(x, ms_in=5, ms_out=12):
    n_in, n_out = min(len(x), RATE * ms_in // 1000), min(len(x), RATE * ms_out // 1000)
    x = x.copy()
    x[:n_in] *= np.linspace(0, 1, n_in)
    x[len(x) - n_out:] *= np.linspace(1, 0, n_out)
    return x


def highpass(x, hz=180.0):
    a = 1.0 / (1.0 + 2 * np.pi * hz / RATE)
    y = np.zeros_like(x)
    for i in range(1, len(x)):
        y[i] = a * (y[i - 1] + x[i] - x[i - 1])
    return y


def piece(p):
    if p[0] == "gap":
        return np.zeros(int(p[1] * RATE))
    name, t0, t1, pitch, gain = p
    x = load(name)
    if t0 is not None:
        x = x[int(t0 * RATE):int(t1 * RATE)]
    if pitch != 1.0:
        x = np.interp(np.arange(0, len(x) - 1, pitch), np.arange(len(x)), x)
    x = highpass(x)
    peak = np.abs(x).max() or 1.0
    return fade(x / peak * gain)


def take(pieces, level):
    x = np.concatenate([piece(p) for p in pieces])
    return np.round(np.clip(x * level, -1, 1) * 32767).astype(np.int16)


def c_array(name, pcm):
    rows = [",".join(str(v) for v in pcm[i:i + 16]) for i in range(0, len(pcm), 16)]
    return "static const int16_t %s[%d] = {\n    %s,\n};\n" % (name, len(pcm), ",\n    ".join(rows))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--wav", help="also write each take as a .wav here")
    args = ap.parse_args()

    out = [
        "/*\n * Generated by sagwa/tools/make_sfx.py -- do not edit.\n"
        " * Real cat recordings (CC0 / public domain, Wikimedia Commons; see ATTRIBUTION.md),\n"
        " * 16 kHz signed 16-bit mono. Each kind has a few takes; sagwa_sfx.c picks one.\n */\n",
        "#pragma once\n#include <stddef.h>\n#include <stdint.h>\n\n",
        "typedef struct {\n    const int16_t *pcm;\n    uint32_t len;\n} sagwa_pcm_take_t;\n\n",
    ]
    total = 0
    for kind, level, takes, what in KINDS:
        names = []
        for i, pieces in enumerate(takes):
            pcm = take(pieces, level)
            total += len(pcm)
            nm = "SAGWA_PCM_%s_%d" % (kind, i)
            names.append((nm, len(pcm)))
            out.append("/* %s, take %d: %.2f s */\n" % (what, i, len(pcm) / RATE))
            out.append(c_array(nm, pcm))
            if args.wav:
                os.makedirs(args.wav, exist_ok=True)
                with wave.open(os.path.join(args.wav, "%s_%d.wav" % (kind.lower(), i)), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(RATE)
                    w.writeframes(pcm.tobytes())
        out.append("static const sagwa_pcm_take_t SAGWA_TAKES_%s[] = {\n%s};\n#define SAGWA_TAKES_%s_N %d\n\n" % (
            kind, "".join("    { %s, %d },\n" % n for n in names), kind, len(names)))
        print("%-13s %d takes, %s" % (kind, len(names), ", ".join("%.2fs" % (n[1] / RATE) for n in names)))

    # The purr loop: kept as it was, so the loop point stays seamless
    purr = np.round(load("legacy:purr_loop.pcm") * 32768).astype(np.int16)
    total += len(purr)
    out.append("/* Continuous purr loop (1.2 s), from purr-original.ogg (CC BY-SA 4.0) */\n")
    out.append("#define SAGWA_PCM_PURR_LOOP_LEN %d\n" % len(purr))
    out.append(c_array("SAGWA_PCM_PURR_LOOP", purr))
    with open(args.out, "w") as f:
        f.write("".join(out))
    print("%d samples, %.0f KB of flash -> %s" % (total, total * 2 / 1024, os.path.relpath(args.out)))


if __name__ == "__main__":
    main()
