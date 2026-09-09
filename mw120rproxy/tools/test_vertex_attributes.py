import json
from pathlib import Path
import tempfile
import unittest
import struct

from imported_map_assets import place, read_material, read_obj, packed_normal
from replay_mesh_math import unpack_frame
from vertex_attributes import color, coverage_flags, atlas_vertices


class VertexAttributeTests(unittest.TestCase):
    def test_shared_glass_vertices_keep_baked_metadata_in_each_fragment(self):
        shared = {"position": [1, 2, 3], "baked_index": 0, "baked_uv": [0.25, 0.75]}
        first = atlas_vertices([shared, shared, shared], 90, 98, [(0, 2048, 1024, 1024)])
        second = atlas_vertices([shared, shared, shared], 90, 98, [(0, 2048, 1024, 1024)])
        self.assertEqual(first, second)
        self.assertEqual(shared["baked_index"], 0)
        self.assertTrue(all(int(v["lightmapUV"][1]) == 102 for v in first + second))
        self.assertIsNot(first[0], first[1])
        self.assertIsNot(first[0], second[0])

    def test_color_defaults_and_validation(self):
        self.assertEqual(color({}), [255] * 4)
        self.assertEqual(color({"color": [0, 64, 128, 200]}), [0, 64, 128, 200])
        for value in ([1, 2, 3], [0, 1, 2, 256], [0, 1, 2, -1], [0, 1, 2, 1.5]):
            with self.assertRaises(ValueError):
                color({"color": value})

    def test_placement_preserves_color(self):
        surface = {
            "vertices": [
                {
                    "position": [1, 2, 3],
                    "uv": [0, 1],
                    "normal_vec": [0, 0, 1],
                    "color": [20, 40, 60, 80],
                }
            ]
        }
        moved = place(surface, [3, 2, 1], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], 2)
        self.assertEqual(moved["vertices"][0]["color"], [20, 40, 60, 80])
        self.assertEqual(moved["vertices"][0]["position"], [5, 6, 7])

    def test_obj_tangents_follow_uvs_and_mirrored_handedness(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "model_export").mkdir()
            path = root / "model_export/fixture_lod0.obj"
            for u, sign in ((1, 1), (-1, -1)):
                path.write_text(
                    f"v 0 0 0\nv 1 0 0\nv 0 0 -1\nvn 0 1 0\nvt 0 1\nvt {u} 1\nvt 0 0\nusemtl wall\nf 1/1/1 2/2/1 3/3/1\n"
                )
                model = read_obj(root, "fixture")[0]
                for vertex in model["vertices"]:
                    self.assertEqual(vertex["binormal_sign"], sign)
                    self.assertEqual(vertex["tangent_vec"], [u, 0, 0])
                placed = place(model, [0, 0, 0], [[0, 1, 0], [-1, 0, 0], [0, 0, 1]], 1)
                for vertex in placed["vertices"]:
                    n, tangent, handedness = unpack_frame(vertex["normal"])
                    self.assertEqual(handedness, sign)
                    self.assertAlmostEqual(tangent[1], u, delta=0.003)
                    self.assertAlmostEqual(n[2], 1, delta=0.003)

    def test_packed_tangents_survive_repeated_model_placement(self):
        model = {
            "vertices": [
                {
                    "position": [1, 2, 3],
                    "uv": [0, 1],
                    "normal": packed_normal([0, 0, 1], [0, 1, 0], -1),
                    "color": [20, 40, 60, 80],
                }
            ]
        }
        placed = place(model, [0, 0, 0], [[0, 1, 0], [-1, 0, 0], [0, 0, 1]], 1)
        n, tangent, sign = unpack_frame(placed["vertices"][0]["normal"])
        self.assertEqual(sign, -1)
        self.assertAlmostEqual(tangent[0], -1, delta=0.003)
        self.assertAlmostEqual(n[2], 1, delta=0.003)

    def test_authored_alpha_comparators_do_not_change_opaque_classes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "materials").mkdir()
            for mode, flag in (("gt0", 8), ("lt128", 16), ("ge128", 0)):
                document = {
                    "techniqueSet": "lm_a0c0",
                    "textures": [{"semantic": "colorMap", "image": "mask"}],
                    "stateBits": [{"colorWriteRgb": True, "alphaTest": mode}],
                }
                (root / "materials/fixture.json").write_text(json.dumps(document))
                material = read_material(root, "fixture")
                self.assertEqual(material["alpha_test_mode"], mode)
                self.assertEqual(coverage_flags({**material, "cutout": True}), flag)
                self.assertEqual(coverage_flags(material), 0)

    def test_direct_lightmap_coordinates_avoid_tile_precision_loss(self):
        f32 = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
        old_error = new_error = 0
        for texel in range(1, 4095):
            coordinate = (texel + 0.37) / 4096
            packed = 200 + 0.25 + coordinate * 0.25
            old = (f32(packed) % 1) * 4 - 1
            new = f32((packed % 1) * 4 - 1)
            old_error = max(old_error, abs(old - coordinate) * 4096)
            new_error = max(new_error, abs(new - coordinate) * 4096)
        self.assertGreater(old_error, 0.1)
        self.assertLess(new_error, 0.0002)


if __name__ == "__main__":
    unittest.main()
