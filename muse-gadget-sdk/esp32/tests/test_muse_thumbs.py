# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Chat image thumbnails: the PNG decoder (components/muse/muse_thumbs.c) and its wiring."""

from __future__ import annotations

import os
import shlex
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MUSE = ROOT / "components" / "muse"


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def png(w: int, h: int, ctype: int, rows: list[bytes], palette: bytes = b"", interlace: int = 0,
        depth: int = 8, filters: list[int] | None = None) -> bytes:
    """A PNG from already filtered rows: each row is its filter byte's data."""
    raw = b"".join(bytes([(filters or [0] * h)[y]]) + rows[y] for y in range(h))
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if palette:
        out += chunk(b"PLTE", palette)
    z = zlib.compress(raw)
    # Two IDATs, as encoders split them
    out += chunk(b"IDAT", z[: len(z) // 2]) + chunk(b"IDAT", z[len(z) // 2:])
    return out + chunk(b"IEND", b"")


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


class MuseThumbsTest(unittest.TestCase):
    binary: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.tmp.name) / "thumbs"
        cc = shlex.split(os.environ.get("CC", "cc"))
        built = subprocess.run(
            [*cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
             "-fno-omit-frame-pointer", "-I", str(MUSE), str(ROOT / "tests" / "muse_thumbs_harness.c"),
             str(MUSE / "muse_thumbs.c"), "-lz", "-o", str(cls.binary)],
            capture_output=True, text=True)
        if built.returncode != 0:
            raise AssertionError(built.stdout + built.stderr)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def decode(self, data: bytes) -> str:
        path = Path(self.tmp.name) / "in.png"
        path.write_bytes(data)
        ran = subprocess.run([str(self.binary), str(path)], capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        return ran.stdout.strip()

    def test_rgb_shrinks_to_the_thumbnail_keeping_its_shape(self) -> None:
        rows = [bytes([200, 100, 50] * 300) for _ in range(150)]
        w, h, centre = self.decode(png(300, 150, 2, rows)).split()
        self.assertEqual((int(w), int(h)), (96, 48))
        self.assertEqual(int(centre, 16), rgb565(200, 100, 50))

    def test_every_filter_reconstructs_the_same_pixels(self) -> None:
        # A gradient, filtered with each of Sub, Up, Average and Paeth by hand
        w, h = 8, 8
        img = [[(x * 30 + y * 3) & 255 for x in range(w)] for y in range(h)]

        def filt(kind: int, y: int) -> bytes:
            out = []
            for x in range(w):
                a = img[y][x - 1] if x else 0
                b = img[y - 1][x] if y else 0
                c = img[y - 1][x - 1] if x and y else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = [0, a, b, (a + b) >> 1, a if pa <= pb and pa <= pc else (b if pb <= pc else c)][kind]
                out.append((img[y][x] - pred) & 255)
            return bytes(out)

        plain = self.decode(png(w, h, 0, [bytes(r) for r in img]))
        for kind in (1, 2, 3, 4):
            rows = [filt(kind, y) for y in range(h)]
            self.assertEqual(self.decode(png(w, h, 0, rows, filters=[kind] * h)), plain, f"filter {kind}")

    def test_alpha_is_laid_over_black(self) -> None:
        rows = [bytes([255, 255, 255, 0] * 4) for _ in range(4)]
        self.assertEqual(self.decode(png(4, 4, 6, rows)).split()[2], "0000")

    def test_palette(self) -> None:
        rows = [bytes([1] * 4) for _ in range(4)]
        out = self.decode(png(4, 4, 3, rows, palette=bytes([0, 0, 0, 10, 200, 30])))
        self.assertEqual(int(out.split()[2], 16), rgb565(10, 200, 30))

    def test_unsupported_and_broken_files_fail_cleanly(self) -> None:
        rows = [bytes([1, 2, 3] * 4) for _ in range(4)]
        self.assertTrue(self.decode(png(4, 4, 2, rows, interlace=1)).startswith("error unsupported"))
        self.assertTrue(self.decode(png(4, 4, 2, [b"\0" * 24] * 4, depth=16)).startswith("error unsupported"))
        self.assertTrue(self.decode(b"GIF89a" + b"\0" * 64).startswith("error not a PNG"))
        good = png(4, 4, 2, rows)
        self.assertTrue(self.decode(good[:-40]).startswith("error"))
        self.assertTrue(self.decode(good[:40] + b"\xff" * 30 + good[70:]).startswith("error"))

    def test_shared_images_reach_the_fetcher_and_the_reply(self) -> None:
        session = (MUSE / "muse_chat_session.cpp").read_text()
        start = session.index('if (!strcmp(event, "delta.presentation"))')
        self.assertIn("muse_thumbs_request(", session[start:start + 1500])
        self.assertIn("muse_ui_add_thumb(", (MUSE / "muse_thumbs.c").read_text())
        self.assertIn('"muse_thumbs.c"', (MUSE / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()
