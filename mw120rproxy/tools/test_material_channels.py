import json
import math
from pathlib import Path
import struct
import tempfile
import unittest

from PIL import Image
from material_channels import describe, directional_light, source_normal, tile_key
from map_lighting import pack_coefficients
from source_atlases import resize_color, resize_data, tint_image


class MaterialChannelsTests(unittest.TestCase):
    def test_faint_color_survives_resize_without_primary_color_fringe(self):
        for alpha in (1, 2, 5, 64, 128):
            image = Image.new("RGBA", (8, 8), (140, 120, 100, alpha))
            for filter in (Image.Resampling.BOX, Image.Resampling.LANCZOS):
                self.assertEqual(
                    resize_color(image, (4, 4), filter).getpixel((0, 0)),
                    (140, 120, 100, alpha),
                )

    def test_transparent_color_is_filtered_without_hidden_rgb_contamination(self):
        image = Image.new("RGBA", (2, 1))
        image.putdata([(140, 120, 100, 255), (0, 255, 0, 0)])
        self.assertEqual(
            resize_color(image, (1, 1), Image.Resampling.BOX).getpixel((0, 0)),
            (140, 120, 100, 128),
        )
        self.assertEqual(
            resize_color(Image.new("RGBA", (2, 2)), (1, 1)).getpixel((0, 0)), (0, 0, 0, 0)
        )

    def test_data_filter_keeps_channels_with_zero_alpha(self):
        image = Image.new("RGBA", (2, 1))
        image.putdata([(200, 40, 60, 0), (20, 160, 80, 200)])
        self.assertEqual(
            resize_data(image, (1, 1), Image.Resampling.BOX).getpixel((0, 0)),
            (110, 100, 70, 100),
        )
        self.assertEqual(
            resize_data(Image.new("RGBA", (2, 2), (130, 129, 50, 0)), (1, 1)).getpixel((0, 0)),
            (130, 129, 50, 0),
        )

    def test_channel_identity_and_constants(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "materials").mkdir()
            (root / "materials/wall.json").write_text(
                json.dumps(
                    {
                        "textures": [
                            {"semantic": "normalMap", "image": "wall_n"},
                            {"semantic": "specularMap", "image": "wall_s"},
                        ],
                        "constants": [{"name": "envMapParms", "literal": [0.8, 2, 5, 0.625]}],
                    }
                )
            )
            m = {"image": "wall", **describe(root, "wall")}
            self.assertEqual(m["environment"], [0.8, 2, 5, 0.625])
            self.assertEqual(m["normal_image"], "wall_n")
            self.assertNotEqual(tile_key(m), tile_key({**m, "normal_image": "other_n"}))
            self.assertNotEqual(tile_key(m), tile_key({**m, "specular_image": None}))

    def test_linear_atlas_mips_keep_zero_alpha_data(self):
        from map_presentation import mipmaps

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pixel = bytes([130, 129, 50, 0])
            (root / "normal.rgba").write_bytes(pixel * 64)
            path = root / "mp_fixture.d3dbsp.material.json"
            path.write_text(
                json.dumps(
                    {
                        "imageDefinitions": [
                            {"rgba8": "normal.rgba", "width": 8, "height": 8, "format": 6}
                        ]
                    }
                )
            )
            mipmaps(root, "mp_fixture", 512)
            self.assertEqual((root / "normal.rgba").read_bytes(), pixel * (64 + 16))
            self.assertEqual(json.loads(path.read_text())["imageDefinitions"][0]["mipCount"], 2)

    def test_raw_constants_use_explicit_table(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "materials").mkdir()
            raw = bytearray(120)
            raw[50] = 1
            struct.pack_into("<I", raw, 60, 64)
            struct.pack_into("<I4f", raw, 64, 84, 0.8, 2, 5, 0.625)
            raw[84:96] = b"envMapParms\0"
            (root / "materials/wall").write_bytes(raw)
            values = describe(root, "wall", raw=root)["environment"]
            for actual, expected in zip(values, [0.8, 2, 5, 0.625]):
                self.assertAlmostEqual(actual, expected)
            raw[50] = 20
            (root / "materials/wall").write_bytes(raw)
            with self.assertRaises(ValueError):
                describe(root, "wall", raw=root)

    def test_iw3_slope_normal_and_directional_coefficients(self):
        n = source_normal(2.08 / 4.08, 2.06451607 / 4.06451607)
        self.assertAlmostEqual(n[2], 1)
        tilted = source_normal(1, 0)
        self.assertAlmostEqual(sum(x * x for x in tilted), 1)
        self.assertGreater(tilted[0], 0)
        self.assertLess(tilted[1], 0)
        first, second = [0.1, 0.2, 0.3], [0.4, 0.2, 0.1]
        result = directional_light(first, second, [1, 0], n)
        expected = [a + b / math.sqrt(1 + 2**2 + 2.06451607**2) for a, b in zip(first, second)]
        for a, b in zip(result, expected):
            self.assertAlmostEqual(a, b)
        self.assertNotEqual(result, directional_light(first, second, [1, 0], tilted))

    def test_lightmap_coefficients_preserve_linear_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "primary").write_bytes(bytes([33]))
            (root / "secondary").write_bytes(bytes([30, 20, 10, 130, 60, 50, 40, 140]))
            world = {
                "lightmaps": [
                    [
                        {"format": 50, "width": 1, "height": 1, "file": "primary"},
                        {"format": 21, "width": 1, "height": 2, "file": "secondary"},
                    ]
                ]
            }
            targets = [Image.new("RGBA", (4, 4)) for _ in range(3)]
            self.assertEqual(pack_coefficients(*targets, root, world, 1, 1), [(0, 1, 1, 1)])
            self.assertEqual(targets[0].getpixel((0, 1)), (10, 20, 30, 33))
            self.assertEqual(targets[1].getpixel((0, 1)), (130, 140, 10, 20))
            self.assertEqual(targets[2].getpixel((0, 1)), (40, 50, 60, 30))

    def test_tint_is_linear_and_alpha_is_not_gamma_encoded(self):
        pixel = tint_image(Image.new("RGBA", (1, 1), (255, 255, 255, 128)), [0.5, 1, 0, 0.5])
        self.assertEqual(pixel.getpixel((0, 0)), (188, 255, 0, 64))
        with self.assertRaises(ValueError):
            tint_image(pixel, [2, 1, 1, 1])


if __name__ == "__main__":
    unittest.main()
