import math
from pathlib import Path
import struct
import tempfile
import unittest
from build_mp_test import COD4, numeric_entities
from cod4_assets import (
    load_model,
    load_surfaces,
    transformed,
    rotation,
    asset_path,
    material_image,
    BlendedMaterial,
)
from create_radiant_starter import box
from radiant_collision import hull, encode, validate, collect, cross, sub, dot
from replay_mesh_math import unpack_normal
from replay_entity_validation import validate_baseline_anchor


class AuthoringTests(unittest.TestCase):
    def test_plane_intersections_are_brush_vertices(self):
        shape = hull(box([-12, -16, 0], [12, 16, 28]), "fixture")
        self.assertEqual(len(shape["vertices"]), 8)
        self.assertEqual(sorted({p[2] for p in shape["vertices"]}), [0, 28])
        self.assertEqual(validate(encode([shape])), 1)

    def test_collision_corruption(self):
        shape = hull(box([-12, -16, 0], [12, 16, 28]), "fixture")
        data = encode([shape])
        for bad in (
            data[:-1],
            data + b"X",
            b"X" + data[1:],
            data[:8] + struct.pack("<I", 4097) + data[12:],
        ):
            with self.assertRaises(ValueError):
                validate(bad)
        shape["vertices"] = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0]]
        with self.assertRaisesRegex(ValueError, "Coplanar"):
            encode([shape])
        shape["vertices"][0][0] = math.nan
        with self.assertRaisesRegex(ValueError, "vertex"):
            encode([shape])

    def test_real_model_rotation_scale_and_winding(self):
        surface = load_model(COD4 / "raw", "com_bunkercrate")[0]
        result = transformed(surface, [10, 20, 30], [0, 90, 0], 2)
        for v, t in zip(surface["vertices"], result["vertices"]):
            p = v["position"]
            expected = [10 - 2 * p[1], 20 + 2 * p[0], 30 + 2 * p[2]]
            for x, y in zip(t["position"], expected):
                self.assertAlmostEqual(x, y, places=5)
            n = v["normal"]
            expected_normal = [-n[1], n[0], n[2]]
            self.assertGreater(dot(unpack_normal(t["normal"]), expected_normal), 0.999)
        self.assertEqual(result["indices"], surface["indices"])
        for i in range(0, len(surface["indices"]), 3):
            a, b, c = [surface["vertices"][j] for j in surface["indices"][i : i + 3]]
            self.assertLess(
                dot(
                    cross(sub(b["position"], a["position"]), sub(c["position"], a["position"])),
                    a["normal"],
                ),
                0,
            )
        with self.assertRaises(ValueError):
            transformed(surface, [0, 0, 0], [0, 0, 0], -1)

    def test_model_truncation(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "bad"
            p.write_bytes(b"\x19\x00\x01\x00")
            with self.assertRaisesRegex(ValueError, "Truncated"):
                load_surfaces(p)

    def test_missing_and_blended_are_explicit(self):
        self.assertEqual(material_image(COD4 / "raw", "mtl_ch_crates"), ("ch_crates_col", False))
        with self.assertRaises(BlendedMaterial):
            material_image(COD4 / "raw", "mtl_ch46e_damaged_glass")
        with self.assertRaises(ValueError):
            asset_path(COD4 / "raw", "xmodel", "../outside")
        with self.assertRaises(FileNotFoundError):
            load_model(COD4 / "raw", "mw120r_nonexistent_test_model")

    def test_original_prefab_expansion(self):
        # The editable mp_test changes between builds; use an isolated
        # prefab fixture so authoring a new map cannot invalidate this test.
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "prefabs").mkdir()
            (root / "prefabs/fixture.map").write_text(
                '{\n"classname" "worldspawn"\n' + box([0, 0, 0], [4, 6, 8]) + "\n}"
            )
            source = root / "mp_fixture.map"
            source.write_text(
                '{\n"classname" "worldspawn"\n'
                + box([-10, -10, -4], [10, 10, 0])
                + "\n}\n"
                + '{ "classname" "misc_prefab" "model" "prefabs/fixture.map" "origin" "10 20 30" "angles" "0 90 0" "modelscale" "2" }'
            )
            shapes, deps = collect(source, root)
            self.assertEqual(len(deps), 2)
            self.assertEqual(len(shapes), 2)
            self.assertEqual(validate(encode(shapes)), 2)
            self.assertAlmostEqual(min(p[0] for p in shapes[1]["vertices"]), -2)
            self.assertAlmostEqual(max(p[2] for p in shapes[1]["vertices"]), 46)

    def test_missing_spawn_rejected(self):
        with self.assertRaisesRegex(ValueError, "Missing required"):
            numeric_entities([{"classname": "worldspawn"}])

    def test_baked_props_keep_a_runtime_snapshot_baseline(self):
        entities = [
            {"classname": cls, "origin": "10 20 16"}
            for cls in ("mp_tdm_spawn", "mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start")
        ]
        entities.append(
            {"classname": "misc_model", "model": "com_bunkercrate", "origin": "50 20 0"}
        )
        text, counts, _ = numeric_entities(entities)
        validate_baseline_anchor(text.encode("ascii"))
        self.assertEqual(text.count('"script_model"'), 1)
        self.assertNotIn('"misc_model"', text)
        self.assertEqual(sum(counts.values()), 3)
        # Reproduce v14's actual exporter regression: valid spawns and baked
        # props, but no remaining entity that can seed the snapshot baseline.
        broken = "\n".join(line for line in text.splitlines() if '"script_model"' not in line)
        with self.assertRaisesRegex(ValueError, "baseline anchor"):
            validate_baseline_anchor(broken.encode("ascii"))


if __name__ == "__main__":
    unittest.main()
