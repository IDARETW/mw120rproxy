import unittest, json, struct, tempfile, math
from pathlib import Path
from PIL import Image
import ladder_data, glass_panes, map_lighting, window_entities
from foliage_material import classify
from glass_material import create

ROOT = Path(__file__).resolve().parents[2]


class RenderingTests(unittest.TestCase):
    def test_footstep_authored_materials_and_walkable_faces(self):
        from footstep_data import surface_type, encode

        root = ROOT / "custom_map_sources/mp_nuketown/extracted"
        self.assertEqual(surface_type(root, "wc/zz_wood_01_all_w"), 21)
        self.assertEqual(surface_type(root, "missing_material"), 5)
        self.assertEqual(surface_type(root, "ap_office_carpet"), 3)
        self.assertEqual(surface_type(root, "ch_grass_01"), 10)

        def triangle(z, kind=0):
            return {
                "indices": [0, 2, 1],
                "vertices": [
                    {"position": [x, y, z], "lightmapUV": [0.25, kind + 0.25]}
                    for x, y in ((0, 0), (64, 0), (0, 64))
                ],
            }

        floor = triangle(0)
        ceiling = triangle(64)
        ceiling["indices"] = [0, 1, 2]
        b, counts = encode([floor, ceiling, triangle(10, 2), triangle(20, 3)], [21])
        self.assertEqual(counts, {21: 1})
        self.assertEqual(len(b), 52)
        self.assertEqual(struct.unpack_from("<I", b, 48)[0], 21)

    def test_authored_and_coplanar_decal_separation(self):
        import copy
        from decal_geometry import separate
        from imported_map_assets import packed_normal

        def surf(name, z=0):
            return {
                "material": name,
                "vertices": [
                    {"position": [x, y, z], "uv": [x, y], "normal": packed_normal([0, 0, 1])}
                    for x, y in ((0, 0), (8, 0), (0, 8))
                ],
                "indices": [0, 2, 1],
            }

        surfaces = [surf(n) for n in ("wall", "trim", "glass", "decal")] + [
            surf("leaf", 5),
            surf("wall", 8),
        ]
        original = copy.deepcopy(surfaces)
        materials = {
            "wall": {},
            "trim": {"cutout": True},
            "glass": {"blended": True},
            "decal": {"cutout": True, "depth_offset": 0.0625},
            "leaf": {"cutout": True},
        }
        report = separate(surfaces, materials)
        self.assertEqual(
            report,
            {"authored_offset_triangles": 1, "coplanar_overlay_triangles": 1, "offset_vertices": 6},
        )
        for i in (0, 2, 4, 5):
            self.assertEqual(surfaces[i], original[i])
        for i in (1, 3):
            for v in surfaces[i]["vertices"]:
                self.assertAlmostEqual(v["position"][2], 0.0625, places=4)

    def test_nuketown_actual_window_overlay_and_truck_offset(self):
        from imported_map_assets import read_material, packed_normal
        from decal_geometry import separate

        root = ROOT / "custom_map_sources/mp_nuketown/extracted"
        self.assertEqual(read_material(root, "wc/zz_trailer_decal")["depth_offset"], 0.0625)
        names = ("wc/zz_wood_01_all_w", "wc/zz_wood_01_cross_w")
        materials = {n: read_material(root, n) for n in names}
        self.assertEqual(materials[names[0]]["depth_offset"], 0)
        world = json.loads((root / "maps/mp/mp_nuketown.d3dbsp.replay-world.json").read_text())
        surfaces = [
            {**s, "vertices": [{**v, "normal": packed_normal(v["normal"])} for v in s["vertices"]]}
            for s in world["surfaces"]
            if s["material"] in names
        ]
        self.assertEqual(separate(surfaces, materials)["coplanar_overlay_triangles"], 80)

    def test_sky_ambient_tracks_linear_image_brightness(self):
        from map_presentation import sky_ambient, ambient_bytes

        dark = sky_ambient([Image.new("RGB", (16, 16), (12, 12, 12))] * 6)
        bright = sky_ambient([Image.new("RGB", (16, 16), (220, 220, 220))] * 6)
        self.assertGreater(bright["diffuse_fill"][0], dark["diffuse_fill"][0] * 2)
        blue = sky_ambient([Image.new("RGB", (16, 16), (20, 90, 255))] * 6)
        self.assertGreater(blue["diffuse_fill"][2], blue["diffuse_fill"][0])
        self.assertLess(blue["diffuse_fill"][2] / blue["diffuse_fill"][0], 2)
        b = ambient_bytes(bright["diffuse_fill"])
        self.assertEqual(len(b), 72)
        for off, c in zip((0, 18, 36), bright["diffuse_fill"]):
            self.assertAlmostEqual(
                struct.unpack_from("<e", b, 8 + off)[0] * 0.886226925, c, delta=0.003
            )
        self.assertGreater(bright["diffuse_fill"][0], 1.0)
        halves = struct.unpack("<32e", b[8:])
        self.assertTrue(all(x == 0 for i, x in enumerate(halves) if i not in (0, 9, 18, 27, 28)))
        self.assertEqual(halves[27:29], (1, 1))
        with self.assertRaises(ValueError):
            ambient_bytes([float("nan"), 0.2, 0.2])

    def test_preview_stretches_without_letterboxing(self):
        from unittest.mock import patch
        from map_presentation import preview

        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            p = root / "package"
            p.mkdir()
            (p / "manifest.json").write_text("{}")
            (root / "images").mkdir()
            (root / "images/loadscreen_mp_stretch.iwi").write_bytes(b"fixture")
            with patch(
                "prepare_textured_mp_test.decode_iwi",
                return_value=[Image.new("RGBA", (16, 32), (201, 43, 89, 255))],
            ):
                preview(p, "mp_stretch", root)
            data = (p / "preview.rgba").read_bytes()
            self.assertEqual(struct.unpack("<4I", data[:16]), (0x4952574D, 1, 1024, 576))
            self.assertEqual(data[16:20], bytes((201, 43, 89, 255)))
            self.assertEqual(data[-4:], data[16:20])

    def test_float32_glass_corner_welding(self):
        pane = {
            "normal": [0.9659258127212524, 0.2588191628456116, 0],
            "vertices": [
                (736.401611328125, -268.3330078125, 300),
                (719.3195190429688, -204.581787109375, 300),
                (719.3195190429688, -204.581787109375, 312),
                (719.319580078125, -204.58203125, 312),
            ],
        }
        data = glass_panes.encode([pane], [{"glassPane": 0}])
        self.assertEqual(glass_panes.validate(data), 1)
        self.assertEqual(struct.unpack_from("<I", data, 16)[0], 3)

    def test_complete_window_damage_chains(self):
        from radiant_source import blocks, properties

        root = ROOT / "custom_map_sources/mp_nuketown/extracted/maps/mp/mp_nuketown.d3dbsp.ents"
        entities = [properties(b) for b in blocks(root.read_text())]
        hidden, intact = window_entities.states(entities)
        by_name = {e.get("targetname"): e for e in entities}
        self.assertIn(id(by_name["pf12_auto1"]), intact)
        for name in ("pf12_auto2", "pf12_auto3", "pf12_auto4"):
            self.assertIn(id(by_name[name]), hidden)
        cycle = [
            {"targetname": "windtrig", "target": "a"},
            {"targetname": "a", "target": "b"},
            {"targetname": "b", "target": "a"},
        ]
        with self.assertRaises(ValueError):
            window_entities.states(cycle)

    def test_split_window_and_collision_ownership(self):
        def face(x, y0, y1, group):
            return {
                "material": "glass",
                "glassGroup": group,
                "vertices": [
                    {"position": [x, y, z]} for y, z in [(y0, 0), (y1, 0), (y1, 80), (y0, 80)]
                ],
                "indices": [0, 1, 2, 0, 2, 3],
            }

        surfaces, panes = glass_panes.prepare(
            [face(-4, 0, 10, 0), face(4, 10, 20, 0), face(0, 100, 120, 1)],
            {"glass": {"blended": True}},
        )
        self.assertEqual(len(panes), 2)
        self.assertEqual([s["glassPane"] for s in surfaces], [0, 0, 1])
        self.assertEqual(panes[0]["halfThickness"], 4)
        self.assertEqual(glass_panes.validate(glass_panes.encode(panes, surfaces)), 2)
        pane_hull = {"vertices": [[x, y, z] for x in (-4, 4) for y in (0, 20) for z in (0, 80)]}
        wall = {"vertices": [[x, y, z] for x in (-20, -5) for y in (0, 20) for z in (0, 80)]}
        frame = {"vertices": [[x, y, z] for x in (-4, 4) for y in (-4, 0) for z in (0, 80)]}
        kept, removed = glass_panes.remove_static_collision([pane_hull, wall, frame], panes)
        self.assertEqual(removed, 1)
        self.assertEqual(kept, [wall, frame])

    def test_measured_ladder_rungs(self):
        root = ROOT / "custom_map_sources/mp_4doffice/extracted"
        collision = json.loads(
            (root / "maps/mp/mp_4doffice.d3dbsp.replay-collision.json").read_text()
        )
        world = json.loads((root / "maps/mp/mp_4doffice.d3dbsp.replay-world.json").read_text())
        faces = [f for b in collision["brushes"] for f in ladder_data.faces(b)]
        self.assertEqual(ladder_data.align_models(faces, world["models"], root), 18)
        self.assertTrue(all(f["rung_distance"] == 24 and 20 < f["grip_width"] < 28 for f in faces))
        self.assertEqual(ladder_data.validate(ladder_data.encode(faces)), 18)

    def test_foliage_alpha_and_specular_mask_separation(self):
        im = Image.new("RGBA", (8, 8), (20, 80, 20, 255))
        im.paste((0, 0, 0, 0), (0, 0, 4, 8))
        a = {"image": "pine_needles", "alpha_test": False, "blended": True}
        classify("tree", a, im)
        self.assertTrue(a["cutout"])
        self.assertFalse(a["blended"])
        b = {"image": "metal_spec", "alpha_test": False}
        classify("metal", b, im)
        self.assertNotIn("cutout", b)
        c = {"image": "glass", "alpha_test": False, "blended": True}
        classify("glass", c, im)
        self.assertTrue(c["blended"])
        self.assertNotIn("cutout", c)

    def test_cutout_depth_writes_without_opaque_prepass(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            stem = "mp_fixture.d3dbsp"
            (folder / (stem + ".techset.json")).write_bytes(
                (
                    ROOT / "custom_map_sources/mp_test/shaders/replay_static_world_techset.json"
                ).read_bytes()
            )
            (folder / (stem + ".material.json")).write_bytes(
                (
                    ROOT / "custom_map_sources/mp_test/dump/maps/mp/mp_test.d3dbsp.material.json"
                ).read_bytes()
            )
            create(folder, stem, "mp_fixture", "foliage")
            ts = json.loads((folder / (stem + ".foliage.techset.json")).read_text())
            self.assertEqual(len(ts["techniques"]), 1)
            self.assertEqual(
                struct.unpack_from("<QQ", bytes.fromhex(ts["techniques"][0]["header"]), 0xA0),
                (0xE00, 0),
            )

    def test_atlas_coverage_prepass_contract(self):
        from replay_depth_coverage import install, prepass_source, shadow_source

        source = (ROOT / "mw120rproxy/tools/map_surface_realtime.hlsl").read_text()
        depth_source = prepass_source(source)
        self.assertIn("asfloat(packed.x", depth_source)
        self.assertNotIn("sunVisibility.Sample", depth_source)
        self.assertIn("SV_TARGET1", depth_source)
        shadow = shadow_source(source)
        self.assertIn("void main(Input input)", shadow)
        self.assertIn("if (kind != 3) return;", shadow)
        self.assertNotIn("SV_TARGET", shadow)
        self.assertNotIn("sunVisibility.Sample", shadow)
        with self.assertRaises(ValueError):
            prepass_source("float4 main() : SV_TARGET0 { return 1; }")
        ts = json.loads(
            (
                ROOT / "custom_map_sources/mp_test/shaders/replay_static_world_techset.json"
            ).read_text()
        )
        install(ts, source, lambda _: b"DXBCcoverage-fixture")
        self.assertEqual(struct.unpack_from("<I", bytes.fromhex(ts["header"]), 8)[0] & 0x1000, 0)
        self.assertEqual(ts["techniques"][0]["shaders"][0], ts["techniques"][-1]["shaders"][0])
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            stem = "mp_fixture.d3dbsp"
            (folder / (stem + ".techset.json")).write_text(json.dumps(ts))
            (folder / (stem + ".material.json")).write_bytes(
                (
                    ROOT / "custom_map_sources/mp_test/dump/maps/mp/mp_test.d3dbsp.material.json"
                ).read_bytes()
            )
            create(folder, stem, "mp_fixture", "foliage")
            result = json.loads((folder / (stem + ".foliage.techset.json")).read_text())
            self.assertEqual(
                struct.unpack_from("<Q", bytes.fromhex(result["header"]), 24)[0],
                (1 << 34) | (1 << 28) | (1 << 27) | 1,
            )
            self.assertEqual(len(result["techniques"]), 4)
            for t, expected in zip(result["techniques"], (0xE00, 0xE20, 0xE20, 0x800)):
                self.assertEqual(
                    struct.unpack_from("<Q", bytes.fromhex(t["header"]), 0xA0)[0], expected
                )
                self.assertEqual(bytes.fromhex(t["header"])[0x9C], 35)
            self.assertEqual(len(bytes.fromhex(result["techniques"][0]["args"])), 24)
            for shadow in result["techniques"][1:3]:
                self.assertEqual(len(bytes.fromhex(shadow["args"])), 24)
                self.assertEqual(bytes.fromhex(shadow["header"])[0x82], 0)
                self.assertEqual(shadow["shaders"][0], result["techniques"][-1]["shaders"][0])

    def test_independent_windows_same_material(self):
        def pane(x):
            return {
                "material": "glass",
                "vertices": [
                    {"position": [x, y, z], "uv": [y / 20, z / 80]}
                    for y, z in [(0, 0), (20, 0), (20, 80), (0, 80)]
                ],
                "indices": [0, 1, 2, 0, 2, 3],
            }

        surfaces, panes = glass_panes.prepare([pane(0), pane(100)], {"glass": {"blended": True}})
        self.assertEqual(len(panes), 2)
        data = glass_panes.encode(panes, surfaces)
        self.assertEqual(glass_panes.validate(data), 2)
        for bad in (
            data[:-1],
            data + b"x",
            data[:24] + struct.pack("<f", float("nan")) + data[28:],
        ):
            with self.assertRaises(ValueError):
                glass_panes.validate(bad)

    def test_lightmap_decode_matches_shipped_shader(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "p").write_bytes(bytes([64, 192]))
            (root / "s").write_bytes(
                bytes([30, 20, 10, 130, 30, 20, 10, 130, 60, 50, 40, 130, 60, 50, 40, 130])
            )
            im = map_lighting.decode(
                root,
                [
                    {"format": 50, "file": "p", "width": 2, "height": 1},
                    {"format": 21, "file": "s", "width": 2, "height": 2},
                ],
            )
            nx = 130 / 255 * 4.08 - 2.08
            ny = 130 / 255 * 4.06451607 - 2.06451607
            weight = 1 / math.sqrt(1 + nx * nx + ny * ny)
            self.assertEqual(
                im.getpixel((0, 0)),
                (
                    round((10 + 40 * weight) / 2),
                    round((20 + 50 * weight) / 2),
                    round((30 + 60 * weight) / 2),
                    64,
                ),
            )
            self.assertEqual(im.getpixel((1, 0))[3], 192)

    def test_float32_lightmap_uv_metadata_precision(self):
        for tile in (0, 109, 170, 249):
            for uv in ((0.1234, 0.8765), (0.9998, 0.0002)):
                packed = struct.unpack(
                    "<2f", struct.pack("<2f", tile + 0.25 + uv[0] * 0.25, 7.25 + uv[1] * 0.25)
                )
                self.assertEqual(math.floor(packed[0]), tile)
                self.assertEqual(math.floor(packed[1]), 7)
                for value, want in zip(packed, uv):
                    self.assertLess(abs((value - math.floor(value)) * 4 - 1 - want), 1 / 4096)


if __name__ == "__main__":
    unittest.main()
