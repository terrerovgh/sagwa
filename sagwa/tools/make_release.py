#!/usr/bin/env python3
"""Gather a Sagwa release: the binaries and the manifest Sagwa updates from.

    make_release.py --version 1.0.12 --notes "commit message" [--build DIR] [--out DIR]

Copies the CoreS3 build (firmware, bootloader, partition table, OTA data) and
the clip pack into --out (the release assets, which flash.py downloads) and
bin/ (so flash.py --bin-dir bin flashes a local build), and writes
--out/manifest.json, which main/sagwa_update.c reads:

    {"version", "fw_sha256", "fw_size", "clips_sha256", "clips_size", "notes"}

Only the standard library.
"""
import argparse
import hashlib
import json
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
ESP32 = os.path.join(ROOT, "muse-gadget-sdk", "esp32")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--notes", default="")
    ap.add_argument("--build", default=os.path.join(ESP32, "build-muse-m5stack-cores3"))
    ap.add_argument("--out", default=os.path.join(ROOT, "dist"))
    args = ap.parse_args()

    bin_dir = os.path.join(ROOT, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    os.makedirs(args.out, exist_ok=True)
    copies = {
        "sagwa-cores3.bin": os.path.join(args.build, "muse-gadget.bin"),
        "bootloader.bin": os.path.join(args.build, "bootloader", "bootloader.bin"),
        "partition-table.bin": os.path.join(args.build, "partition_table", "partition-table.bin"),
        "ota_data_initial.bin": os.path.join(args.build, "ota_data_initial.bin"),
        # The clip pack pack_clips.py writes, beside the firmware that embeds it
        "sagwa_clips.bin": os.path.join(ESP32, "components", "muse", "avatar", "sagwa_clips.bin"),
    }
    for name, src in copies.items():
        shutil.copyfile(src, os.path.join(bin_dir, name))
        shutil.copyfile(src, os.path.join(args.out, name))
    clips = os.path.join(args.out, "sagwa_clips.bin")

    fw = os.path.join(args.out, "sagwa-cores3.bin")
    notes = " ".join(args.notes.split())[:120]
    manifest = {
        "version": args.version,
        "fw_sha256": sha256(fw),
        "fw_size": os.path.getsize(fw),
        "clips_sha256": sha256(clips),
        "clips_size": os.path.getsize(clips),
        "notes": notes,
    }
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print(json.dumps(manifest, indent=1))


if __name__ == "__main__":
    main()
