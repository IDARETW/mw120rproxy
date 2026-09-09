import struct
import unittest

import radiant_collision as collision


class CollisionContentsTests(unittest.TestCase):
    def setUp(self):
        self.points = [[x, y, z] for x in (0, 32) for y in (0, 32) for z in (0, 32)]

    def test_brush_contents_survive_encoding(self):
        masks = [collision.SOLID, collision.PLAYER_CLIP, collision.SHOT_CLIP]
        data = collision.encode(
            [{"vertices": self.points, "contents": c | 0x08000000} for c in masks]
        )
        self.assertEqual(data[:8], b"MWCOLL03")
        self.assertEqual(collision.validate(data), 3)
        for i, expected in enumerate(masks):
            self.assertEqual(struct.unpack_from("<II", data, 12 + i * 104), (8, expected))

    def test_clip_tools_do_not_become_solid(self):
        self.assertEqual(
            collision.contents({"textures": ["clip_player", "caulk"]}), collision.PLAYER_CLIP
        )
        self.assertEqual(collision.contents({"textures": ["clip_shot"]}), collision.SHOT_CLIP)
        self.assertEqual(collision.contents({"textures": ["concrete"]}), collision.SOLID)
        self.assertEqual(collision.contents({"contents": 0x40000000}), 0)

    def test_legacy_formats_and_invalid_contents(self):
        data = collision.encode([{"vertices": self.points}])
        legacy = b"MWCOLL02" + data[8:16] + data[20:]
        self.assertEqual(collision.validate(legacy), 1)
        box = b"MWCOLL01" + struct.pack("<I6f", 1, 0, 0, 0, 32, 32, 32)
        self.assertEqual(collision.validate(box), 1)
        for mask in (0, 0x80000000):
            invalid = data[:16] + struct.pack("<I", mask) + data[20:]
            with self.assertRaises(ValueError):
                collision.validate(invalid)
        for length in (16, 19, len(data) - 1):
            with self.assertRaises(ValueError):
                collision.validate(data[:length])
        with self.assertRaises(ValueError):
            collision.encode([{"vertices": self.points, "contents": 0x40000000}])

    def test_compiled_triangle_contents_and_legacy_fallback(self):
        geometry = {
            "vertices": [[0, 0, 0], [8, 0, 0], [0, 8, 0]],
            "triangles": [[0, 1, 2]] * 4,
            "triangle_contents": [collision.SHOT_CLIP, collision.PLAYER_CLIP, 0, None],
        }
        hulls, report = [], {}
        self.assertEqual(collision.add_compiled_triangles(hulls, geometry, report), 2)
        self.assertEqual(
            [h["contents"] for h in hulls], [collision.SHOT_CLIP, collision.PLAYER_CLIP]
        )
        self.assertEqual(report["default_solid"], 0)
        self.assertEqual(report["unreferenced_triangles"], 1)
        self.assertEqual(report["skipped_contents"], 1)
        self.assertEqual(collision.validate(collision.encode(hulls)), 2)
        geometry["triangle_contents"].pop()
        with self.assertRaises(ValueError):
            collision.add_compiled_triangles([], geometry)
        del geometry["triangle_contents"]
        self.assertEqual(collision.add_compiled_triangles([], geometry, report), 4)
        self.assertEqual(report["default_solid"], 4)


if __name__ == "__main__":
    unittest.main()
