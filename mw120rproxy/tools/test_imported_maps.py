import unittest
import struct
from pathlib import Path
from imported_map_assets import brush_hull, place, read_obj, read_material, safe_asset
from build_imported_map import merge_surfaces
from replay_mesh_math import unpack_normal
import radiant_collision
from prepare_cod4_map import read_bsp
from prepare_textured_mp_test import decode_iwi


class ImportedMapTests(unittest.TestCase):
    def test_nuketown_unlit_cutout_and_recovered_wavelet(self):
        root = Path(__file__).resolve().parents[2] / "custom_map_sources/mp_nuketown/extracted"
        self.assertTrue(read_material(root, "mc/mtl_perc_doubletap")["alpha_test"])
        for name in ("specialty_rof_256", "specialty_bulletdamage_256"):
            image = decode_iwi((root / "images" / (name + ".iwi")).read_bytes())[0]
            self.assertEqual(image.size, (256, 256))
            lo, hi = image.getchannel("A").getextrema()
            self.assertLess(lo, 128)
            self.assertGreaterEqual(hi, 128)

    def test_uncompressed_non_power_of_two_iwi(self):
        pixels = bytes([0, 0, 255, 0, 255, 0, 255, 0, 0])
        size = 28 + len(pixels)
        data = b"IWi\x06" + bytes([2, 2]) + struct.pack("<3H4I", 3, 1, 1, *([size] * 4)) + pixels
        image = decode_iwi(data)[0]
        self.assertEqual(image.size, (3, 1))
        self.assertEqual(image.getpixel((0, 0)), (255, 0, 0, 255))
        self.assertEqual(image.getpixel((2, 0)), (0, 0, 255, 255))
        with self.assertRaises(ValueError):
            decode_iwi(data[:-1])

    def test_bsp_lump_seven_alignment_after_odd_lump_six(self):
        data = b"IBSP" + struct.pack("<II4I", 22, 2, 6, 1, 7, 4) + b"X" + b"\xab" * 3 + b"DATA"
        lumps, entries = read_bsp(data)
        self.assertEqual(lumps[7], b"DATA")
        self.assertEqual(entries[1]["offset"], 32)
        with self.assertRaises(ValueError):
            read_bsp(data + b"BAD")

    def test_compiled_axial_and_bevel_brush(self):
        b = {"mins": [0, 0, 0], "maxs": [10, 10, 10], "planes": [], "contents": 1}
        h = brush_hull(b, 0)
        self.assertEqual(len(h["vertices"]), 8)
        self.assertEqual(radiant_collision.validate(radiant_collision.encode([h])), 1)
        b["planes"] = [[1, 1, 0, 10]]
        h = brush_hull(b, 1)
        self.assertEqual(len(h["vertices"]), 6)
        self.assertTrue(all(p[0] + p[1] <= 10.001 for p in h["vertices"]))

    def test_native_placement_basis_and_normals(self):
        s = {
            "material": "fixture",
            "vertices": [{"position": [2, 3, 4], "normal_vec": [1, 0, 0], "uv": [0.2, 0.3]}],
            "indices": [],
        }
        p = place(s, [10, 20, 30], [[0, 1, 0], [-1, 0, 0], [0, 0, 1]], 2)["vertices"][0]
        self.assertEqual(p["position"], [4, 24, 38])
        self.assertGreater(unpack_normal(p["normal"])[1], 0.999)

    def test_merge_preserves_materials_and_rebases_indices(self):
        a = {"material": "a", "vertices": [{}] * 3, "indices": [0, 2, 1]}
        b = {"material": "b", "vertices": [{}] * 3, "indices": [0, 1, 2]}
        merged = merge_surfaces([a, b, a])
        self.assertEqual(len(merged), 2)
        self.assertEqual(merged[0]["indices"], [0, 2, 1, 3, 5, 4])

    def test_paths_are_contained(self):
        for name in ("../outside", "/absolute", "x:y", "a\\b"):
            with self.assertRaises(ValueError):
                safe_asset(Path("."), "models", name, ".obj")

    def test_office_extracted_custom_material_and_obj(self):
        root = Path(__file__).resolve().parents[2] / "custom_map_sources/mp_4doffice/extracted"
        self.assertEqual(read_material(root, "logo4d")["image"], "logo4d")
        surfaces = read_obj(root, "ad_sodamachine")
        self.assertTrue(surfaces)
        for s in surfaces:
            self.assertLess(max(s["indices"]), len(s["vertices"]))


if __name__ == "__main__":
    unittest.main()
