import copy
import unittest
from PIL import Image
from foliage_mesh import mask_rectangles, mask_geometry, cutout
from replay_mesh_math import pack, quaternion, unpack_normal
from cod4_assets import material_image, ShadowOnlyMaterial, BlendedMaterial
from build_mp_test import COD4

NORMAL = pack(*quaternion([1, 0, 0], [0, 1, 0], [0, 0, 1]))


def surface(uvs=((0, 0), (1, 0), (1, 1), (0, 1))):
    return {
        "material": "fixture",
        "vertices": [
            {"position": [10 + u * 2, 20 + v * 3, 7], "uv": [u, v], "normal": NORMAL}
            for u, v in uvs
        ],
        "indices": [0, 1, 2, 0, 2, 3],
    }


def mesh_polygons(parts):
    import shapely

    return [
        shapely.Polygon([s["vertices"][i]["uv"] for i in s["indices"][n : n + 3]])
        for s in parts
        for n in range(0, len(s["indices"]), 3)
    ]


class FoliageTests(unittest.TestCase):
    def test_tree_material_flags(self):
        for name in ("mtl_pine", "mtl_pine_canopy"):
            self.assertEqual(material_image(COD4 / "raw", name), ("tree_pine_col", True))
        with self.assertRaises(ShadowOnlyMaterial):
            material_image(COD4 / "raw", "mtl_tree_shadow_caster")
        self.assertEqual(material_image(COD4 / "raw", "mtl_ch_crates"), ("ch_crates_col", False))
        with self.assertRaises(BlendedMaterial):
            material_image(COD4 / "raw", "mtl_ch46e_damaged_glass")

    def test_hole_coverage_and_transformed_positions(self):
        image = Image.new("RGBA", (16, 16), (255, 255, 255, 255))
        image.paste((0, 0, 0, 0), (4, 4, 12, 12))
        rectangles, size = mask_rectangles(image)
        mask = mask_geometry(rectangles, size)
        import shapely

        src = surface()
        before = copy.deepcopy(src)
        output = cutout(src, mask, two_sided=False)
        union = shapely.union_all(mesh_polygons(output))
        self.assertLess(union.symmetric_difference(mask).area, 1e-9)
        self.assertFalse(union.covers(shapely.Point(0.5, 0.5)))
        self.assertEqual(src, before)
        for s in output:
            for v in s["vertices"]:
                u, w = v["uv"]
                self.assertAlmostEqual(v["position"][0], 10 + 2 * u)
                self.assertAlmostEqual(v["position"][1], 20 + 3 * w)
                self.assertAlmostEqual(v["position"][2], 7)

    def test_two_sided_winding_and_normals(self):
        mask = mask_geometry([(0, 0, 1, 1)], (16, 16))
        for mirrored in (False, True):
            src = surface()
            if mirrored:
                src["indices"] = [0, 2, 1, 0, 3, 2]
            parts = cutout(src, mask)
            self.assertEqual(sum(len(s["indices"]) // 3 for s in parts), 4)
            for s in parts:
                for n in range(0, len(s["indices"]), 6):
                    front = [s["vertices"][i] for i in s["indices"][n : n + 3]]
                    back = [s["vertices"][i] for i in s["indices"][n + 3 : n + 6]]
                    self.assertEqual(
                        [v["position"] for v in front],
                        list(reversed([v["position"] for v in back])),
                    )
                    a, b, c = [v["uv"] for v in front]
                    signed = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
                    self.assertGreater(signed * (-1 if mirrored else 1), 0)
                    nf = unpack_normal(front[0]["normal"])
                    nb = unpack_normal(back[0]["normal"])
                    self.assertLess(sum(a * b for a, b in zip(nf, nb)), -0.999)

    def test_negative_and_repeated_uvs(self):
        mask = mask_geometry([(0, 0, 0.5, 1)], (16, 16))
        import shapely

        src = surface(((-1, 0), (1, 0), (1, 1), (-1, 1)))
        output = cutout(src, mask, two_sided=False)
        union = shapely.union_all(mesh_polygons(output))
        expected = shapely.union_all([shapely.box(-1, 0, -0.5, 1), shapely.box(0, 0, 0.5, 1)])
        self.assertLess(union.symmetric_difference(expected).area, 1e-9)

    def test_line_uvs_clip_plant_stem_geometry(self):
        mask = mask_geometry([(0, 0, 0.5, 1)], (16, 16))
        src = surface()
        for v in src["vertices"]:
            v["uv"][1] = 0.5
        parts = cutout(src, mask, two_sided=False)
        self.assertTrue(parts)
        for s in parts:
            self.assertTrue(all(v["position"][0] <= 11.000001 for v in s["vertices"]))
        self.assertTrue(
            any(abs(v["position"][0] - 11) < 1e-6 for s in parts for v in s["vertices"])
        )

    def test_transparent_mask_and_budget_guards(self):
        rectangles, size = mask_rectangles(Image.new("RGBA", (16, 16), (0, 0, 0, 0)))
        self.assertEqual(cutout(surface(), mask_geometry(rectangles, size)), [])
        mask = mask_geometry([(0, 0, 1, 1)], (16, 16))
        with self.assertRaisesRegex(ValueError, "budget"):
            cutout(surface(), mask, triangle_budget=1)
        src = surface()
        for v in src["vertices"]:
            v["uv"] = [0.5, 0.5]
        self.assertEqual(sum(len(s["indices"]) // 3 for s in cutout(src, mask)), 4)
        self.assertEqual(cutout(src, mask_geometry([], (16, 16))), [])
        self.assertEqual(cutout(surface(((0, 0), (0, 0), (0, 0), (0, 0))), mask), [])
        with self.assertRaisesRegex(ValueError, "64 times"):
            cutout(surface(((0, 0), (100, 0), (100, 100), (0, 100))), mask)

    def test_mesh_splits_before_16_bit_index_limit(self):
        mask = mask_geometry([(0, 0, 1, 1)], (16, 16))
        src = surface()
        src["indices"] *= 5001
        parts = cutout(src, mask)
        self.assertGreater(len(parts), 1)
        self.assertEqual(sum(len(s["indices"]) // 3 for s in parts), 20004)
        for s in parts:
            self.assertLessEqual(len(s["vertices"]), 60000)
            self.assertLess(max(s["indices"]), len(s["vertices"]))


if __name__ == "__main__":
    unittest.main()
