import copy
import math
import unittest

from model_internal_faces import remove_internal_faces


def box(lo, hi):
    result = {"material": "wood", "vertices": [], "indices": []}
    for axis in range(3):
        x, y = [k for k in range(3) if k != axis]
        for side in (0, 1):
            normal = [0, 0, 0]
            normal[axis] = 1 if side else -1
            points = []
            for a, b in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = list(lo)
                p[axis] = hi[axis] if side else lo[axis]
                p[x], p[y] = (hi[x] if a else lo[x]), (hi[y] if b else lo[y])
                points.append(p)
            if side == (axis != 1):
                points.reverse()
            first = len(result["vertices"])
            result["vertices"].extend(
                {"position": p, "normal_vec": normal, "uv": [p[0] + p[2], p[1]]} for p in points
            )
            result["indices"].extend(first + i for i in (0, 1, 2, 0, 2, 3))
    return result


def merged(a, b):
    return {
        **a,
        "vertices": a["vertices"] + b["vertices"],
        "indices": a["indices"] + [len(a["vertices"]) + i for i in b["indices"]],
    }


def area(mesh):
    total = 0
    for off in range(0, len(mesh["indices"]), 3):
        a, b, c = [mesh["vertices"][i]["position"] for i in mesh["indices"][off : off + 3]]
        u, v = [b[k] - a[k] for k in range(3)], [c[k] - a[k] for k in range(3)]
        total += (
            math.sqrt(
                sum(
                    (u[(k + 1) % 3] * v[(k + 2) % 3] - u[(k + 2) % 3] * v[(k + 1) % 3]) ** 2
                    for k in range(3)
                )
            )
            / 2
        )
    return total


class InternalFacesTests(unittest.TestCase):
    def test_partial_touching_faces_keep_exterior_and_uv(self):
        mesh = merged(box((0, 0, 0), (2, 2, 2)), box((2, 1, 1), (4, 3, 3)))
        original = copy.deepcopy(mesh)
        output, report = remove_internal_faces(mesh)
        self.assertEqual(mesh, original)
        self.assertAlmostEqual(report["removed_area"], 2)
        self.assertAlmostEqual(area(output), area(mesh) - 2)
        for vertex in output["vertices"]:
            p = vertex["position"]
            self.assertAlmostEqual(vertex["uv"][0], p[0] + p[2])
            self.assertAlmostEqual(vertex["uv"][1], p[1])
            self.assertTrue(all(-1e-6 <= c <= upper + 1e-6 for c, upper in zip(p, (4, 3, 3))))
        # All non-contact planes retain their entire exterior area.
        for axis, plane in ((0, 0), (0, 4), (1, 0), (1, 3), (2, 0), (2, 3)):

            def on_plane(source):
                indices = []
                for off in range(0, len(source["indices"]), 3):
                    ids = source["indices"][off : off + 3]
                    if all(
                        abs(source["vertices"][i]["position"][axis] - plane) < 1e-6 for i in ids
                    ):
                        indices.extend(ids)
                return {**source, "indices": indices}

            self.assertAlmostEqual(area(on_plane(mesh)), area(on_plane(output)))

    def test_open_cards_and_separated_solids_are_unchanged(self):
        closed = box((0, 0, 0), (2, 2, 2))
        open_mesh = {**closed, "indices": closed["indices"][:-3]}
        separated = merged(closed, box((2.001, 1, 1), (4, 3, 3)))
        for source in (closed, open_mesh, separated):
            output, report = remove_internal_faces(source)
            self.assertIs(output, source)
            self.assertEqual(report["removed_area"], 0)


if __name__ == "__main__":
    unittest.main()
