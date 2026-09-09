import copy
import math
import unittest

from source_tjunctions import stitch


def vertex(x, y, z=0, uv=None):
    return {
        "position": [x, y, z],
        "uv": uv or [x, y],
        "normal": [0, 0, 1],
        "tangent": [1, 0, 0],
        "binormal_sign": 1,
        "color": [255, 255, 255, 255],
        "lightmap_uv": [x / 8, y / 8],
    }


def surface(vertices, indices=None):
    return {
        "material": "wall",
        "vertices": vertices,
        "indices": indices if indices is not None else list(range(len(vertices))),
    }


def signed_area(s):
    total = 0
    for i in range(0, len(s["indices"]), 3):
        a, b, c = [s["vertices"][j]["position"] for j in s["indices"][i : i + 3]]
        area = ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2
        if area <= 0:
            raise AssertionError("Degenerate or reversed triangle")
        total += area
    return total


class SourceJunctionTests(unittest.TestCase):
    def test_shared_endpoint_preserves_shape_winding_and_attributes(self):
        wall = surface([vertex(0, 0), vertex(8, 0), vertex(0, 8)])
        neighbor = surface([vertex(4, 0), vertex(0, 0), vertex(4, -4)])
        original = copy.deepcopy(wall["vertices"])
        report = stitch([wall, neighbor])
        self.assertGreater(report["added_triangles"], 0)
        self.assertEqual(wall["vertices"][:3], original)
        self.assertAlmostEqual(signed_area(wall), 32)
        junction = next(v for v in wall["vertices"] if v["position"] == [4, 0, 0])
        self.assertEqual(junction["uv"], [4, 0])
        self.assertEqual(junction["lightmap_uv"], [0.5, 0])
        self.assertEqual(junction["color"], [255] * 4)
        self.assertEqual(stitch([wall, neighbor])["added_triangles"], 0)

    def test_multiple_edges_and_points_preserve_area(self):
        wall = surface([vertex(0, 0), vertex(8, 0), vertex(0, 8)])
        points = surface([vertex(2, 0), vertex(6, 0), vertex(4, 4), vertex(0, 4)], [])
        stitch([wall, points])
        self.assertAlmostEqual(signed_area(wall), 32)
        positions = {tuple(v["position"]) for v in wall["vertices"]}
        for point in points["vertices"]:
            self.assertIn(tuple(point["position"]), positions)

    def test_quantized_near_endpoint_and_intentional_gap(self):
        wall = surface([vertex(0, 0), vertex(8, 0), vertex(0, 8)])
        points = surface([vertex(4, 0.001), vertex(6, 0.02)], [])
        report = stitch([wall, points])
        positions = {tuple(v["position"]) for v in wall["vertices"]}
        self.assertIn((4, 0.001, 0), positions)
        self.assertNotIn((6, 0.02, 0), positions)
        self.assertAlmostEqual(report["maximum_edge_adjustment"], 0.001)
        self.assertGreater(signed_area(wall), 0)

    def test_no_junction_is_byte_for_byte_unchanged(self):
        surfaces = [surface([vertex(0, 0), vertex(1, 0), vertex(0, 1)])]
        original = copy.deepcopy(surfaces)
        self.assertEqual(stitch(surfaces)["added_triangles"], 0)
        self.assertEqual(surfaces, original)

    def test_invalid_input_rejected(self):
        with self.assertRaises(ValueError):
            stitch([], tolerance=0.1)
        with self.assertRaises(ValueError):
            stitch([surface([vertex(math.inf, 0)], [])])


if __name__ == "__main__":
    unittest.main()
