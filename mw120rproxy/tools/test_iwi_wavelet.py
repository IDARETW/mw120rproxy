from pathlib import Path
import struct
import unittest

from prepare_textured_mp_test import decode_iwi


def iwi(format, width, height, payload, flags=0, depth=1):
    size = 28 + len(payload)
    return (
        b"IWi\x06"
        + bytes((format, flags))
        + struct.pack("<3H4I", width, height, depth, size, 0, 0, 0)
        + payload
    )


class WaveletTests(unittest.TestCase):
    def test_all_wavelet_base_channels(self):
        for format, payload, expected in (
            (6, bytes((10, 20, 30, 40)), (30, 20, 10, 40)),
            (7, bytes((10, 20, 30)), (30, 20, 10, 255)),
            (8, bytes((37, 91)), (37, 37, 37, 91)),
            (9, bytes((37,)), (37, 37, 37, 255)),
            (10, bytes((91,)), (255, 255, 255, 91)),
        ):
            with self.subTest(format=format):
                image = decode_iwi(iwi(format, 1, 1, payload, flags=2))[0]
                self.assertEqual(image.getpixel((0, 0)), expected)

    def test_wavelet_reconstruction_preserves_luminance_and_alpha(self):
        bits = []
        for value, width in (
            (0, 1),
            (0, 1),
            (4, 5),
            (1, 3),
            (1, 3),
            (0, 1),
            (1, 1),
            (1, 1),
            (1, 1),
        ):
            bits.extend((value >> bit) & 1 for bit in range(width))
        encoded = sum(value << bit for bit, value in enumerate(bits)).to_bytes(
            (len(bits) + 7) // 8, "little"
        )
        image = decode_iwi(iwi(8, 2, 2, bytes((50, 90)) + encoded))[0]
        self.assertEqual(list(image.getdata()), [(52, 52, 52, 90)] * 2 + [(48, 48, 48, 90)] * 2)

    def test_cubemap_faces_and_zero_depth(self):
        pixels = [(i * 20, 255 - i) for i in range(6)]
        data = iwi(8, 1, 1, bytes(value for pixel in pixels for value in pixel), flags=6, depth=0)
        faces = decode_iwi(data)
        self.assertEqual(len(faces), 6)
        self.assertEqual(
            [face.getpixel((0, 0)) for face in faces], [(l, l, l, a) for l, a in pixels]
        )

    def test_truncated_wavelet_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "wavelet"):
            decode_iwi(iwi(8, 2, 2, bytes((50, 90))))


if __name__ == "__main__":
    unittest.main()
