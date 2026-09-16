"""Offline tests: IWI mip/cube decoding, archive lookup, and Replay sky winding."""

import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_textured_mp_test import decode_iwi, find_images, sky_surfaces, NAMES


def fixture_iwi(colors, width=4, lower_mips=b"", fmt=11):
    size = max(1, (width + 3) // 4) ** 2
    payload = b""
    for color in colors:
        bc = struct.pack("<HHI", color, 0, 0)
        if fmt == 12:
            bc = b"\xff" * 8 + bc
        if fmt == 13:
            bc = bytes([255, 0]) + b"\0" * 6 + bc
        payload += bc * size
    flags = (4 if len(colors) == 6 else 0) | (0 if lower_mips else 3)
    length = 28 + len(lower_mips) + len(payload)
    return (
        b"IWi\x06"
        + bytes([fmt, flags])
        + struct.pack("<3H4I", width, width, 1, length, 28, 28, 28)
        + lower_mips
        + payload
    )


class TextureTests(unittest.TestCase):
    def test_top_mip_not_thumbnail(self):
        for fmt in (11, 12, 13):
            data = fixture_iwi([0xF800], 8, b"\0" * 32, fmt)
            (face,) = decode_iwi(data)
            self.assertEqual(face.size, (8, 8))
            self.assertEqual(face.getpixel((7, 7)), (255, 0, 0, 255))

    def test_cube_face_order(self):
        images = decode_iwi(fixture_iwi([0xF800, 0x07E0, 0x001F, 0xFFFF, 0, 0xFFE0]))
        self.assertEqual(
            [im.getpixel((2, 2))[:3] for im in images],
            [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255), (0, 0, 0), (255, 255, 0)],
        )

    def test_truncation_and_bad_header(self):
        good = fixture_iwi([0xF800])
        for bad in (good[:-1], good + b"\0", b"IWi\x08" + good[4:], good[:6] + b"\0\0" + good[8:]):
            with self.assertRaises(ValueError):
                decode_iwi(bad)

    def test_archive_only_reads_exact_images(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "iw_01.iwd"
            with zipfile.ZipFile(archive, "w") as z:
                for name in NAMES:
                    z.writestr("images/" + name + ".iwi", fixture_iwi([0xF800]))
                z.writestr("../unrelated.txt", "must not extract")
            found = find_images(root)
            self.assertEqual(set(found), {n + ".iwi" for n in NAMES})
            self.assertEqual(list(root.iterdir()), [archive])

    def test_missing_assets_fail_explicitly(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(FileNotFoundError, "chechnya_ft"):
                find_images(Path(temp))

    def test_sky_clockwise_front_faces_visible_from_inside(self):
        for surface in sky_surfaces():
            v = surface["vertices"]
            indices = surface["indices"]
            for offset in (0, 3):
                a, b, c = [v[i]["position"] for i in indices[offset : offset + 3]]
                ab = [b[k] - a[k] for k in range(3)]
                ac = [c[k] - a[k] for k in range(3)]
                cross = [
                    ab[1] * ac[2] - ab[2] * ac[1],
                    ab[2] * ac[0] - ab[0] * ac[2],
                    ab[0] * ac[1] - ab[1] * ac[0],
                ]
                # BSP's clockwise front faces oppose their geometric cross.
                self.assertGreater(sum(cross[k] * a[k] for k in range(3)), 0)


if __name__ == "__main__":
    unittest.main()
