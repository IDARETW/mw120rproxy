import base64
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT.parent / "mw120rproxy/tools"))
from import_map import detect, parser, run
from mapimport.assets import Assets
from mapimport.bsp import BSP, read_q2, read_q3
from mapimport.cod import read_cod
from mapimport.core import (
    Scene,
    cross,
    dot,
    encode_collision,
    entities,
    hull_from_planes,
    safe_path,
    sub,
    triangulate,
)
from mapimport.mesh import node_matrix, read_gltf, read_obj
from mapimport.replay import packed_normal, spawns, split_surfaces
from mapimport.source import displacement_indices, read_source

DOWNLOADS = ROOT / "evidence/multi_engine_20260908/downloads"
REPLAY = Path(os.environ.get("IW8_REPLAY_EXE", ROOT / "missing-replay.exe"))
REPLAY_ARGS = ["--replay", str(REPLAY)]


def q3_fixture(patch=False):
    chunks = [b"" for _ in range(17)]
    chunks[0] = (
        b'{"classname" "worldspawn"}\n{"classname" "info_player_deathmatch" "origin" "0 0 32"}\0'
    )
    chunks[1] = struct.pack("<64sii", b"test", 0, 1)
    ps = [
        [-1, 0, 0, 64],
        [1, 0, 0, 64],
        [0, -1, 0, 64],
        [0, 1, 0, 64],
        [0, 0, -1, 8],
        [0, 0, 1, 0],
    ]
    chunks[2] = b"".join(struct.pack("<4f", *p) for p in ps)
    chunks[7] = struct.pack("<6f4i", -64, -64, -8, 64, 64, 64, 0, 1, 0, 1)
    chunks[8] = struct.pack("<3i", 0, 6, 0)
    chunks[9] = b"".join(struct.pack("<2i", i, 0) for i in range(6))
    verts = (
        [
            (x * 32, y * 32, 16 if x == 1 and y == 1 else 0)
            for y in range(3)
            for x in range(3)
        ]
        if patch
        else [(0, 0, 0), (32, 0, 0), (0, 32, 0)]
    )
    chunks[10] = b"".join(
        struct.pack("<10f4B", *p, 0, 0, 0, 0, 0, 0, 1, 255, 255, 255, 255)
        for p in verts
    )
    chunks[11] = b"" if patch else struct.pack("<3i", 0, 1, 2)
    face = (
        [0, 0, 2 if patch else 1, 0, len(verts), 0, 0 if patch else 3, -1, 0, 0, 0, 0]
        + [0.0] * 12
        + [3 if patch else 0, 3 if patch else 0]
    )
    chunks[13] = struct.pack("<12i12f2i", *face)
    header = bytearray(b"IBSP" + struct.pack("<i", 46))
    body = bytearray()
    for c in chunks:
        header.extend(struct.pack("<ii", 144 + len(body), len(c)))
        body.extend(c)
    return bytes(header + body)


class ImportTests(unittest.TestCase):
    def test_source_displacement_topology_matches_center_fans(self):
        # A 2x2 cell block fans eight triangles around vertex (1, 1).
        # Checking the undirected edges catches the visibly different uniform split.
        indices = displacement_indices(3)
        triangles = [indices[i : i + 3] for i in range(0, len(indices), 3)]
        self.assertEqual(len(triangles), 8)
        self.assertTrue(all(4 in t for t in triangles))
        edges = {
            tuple(sorted((t[i], t[(i + 1) % 3]))) for t in triangles for i in range(3)
        }
        self.assertTrue({(0, 4), (2, 4), (4, 6), (4, 8)} <= edges)
        self.assertFalse({(1, 3), (1, 5), (3, 7), (5, 7)} & edges)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def file(self, name, data):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data.encode() if isinstance(data, str) else data)
        return p

    def test_entities_comments_and_escapes(self):
        self.assertEqual(
            entities('// test\n {"classname" "worldspawn" "message" "a\\"b"}')[0][
                "message"
            ],
            'a"b',
        )

    def test_entities_reject_unclosed(self):
        with self.assertRaises(ValueError):
            entities('{"classname" "worldspawn"')

    def test_entities_reject_hidden_tail(self):
        with self.assertRaises(ValueError):
            entities('{"classname" "worldspawn"}garbage')

    def test_entities_reject_duplicate_keys(self):
        with self.assertRaises(ValueError):
            entities('{"a" "1" "a" "2"}')

    def test_path_containment(self):
        for p in ("../a", "/etc/passwd", "C:/a", "a:stream", "a//b"):
            with self.subTest(p=p), self.assertRaises(ValueError):
                safe_path(self.root, p)

    def test_concave_polygon_area(self):
        p = [[0, 0, 0], [3, 0, 0], [3, 3, 0], [1, 1, 0], [0, 3, 0]]
        ix = triangulate(p)
        area = sum(
            abs(cross(sub(p[ix[i + 1]], p[ix[i]]), sub(p[ix[i + 2]], p[ix[i]]))[2]) / 2
            for i in range(0, len(ix), 3)
        )
        self.assertAlmostEqual(area, 6)

    def test_unbounded_brush_rejected(self):
        with self.assertRaises(ValueError):
            hull_from_planes([[1, 0, 0, 1]] * 4)

    def test_hull_solid_volume(self):
        ps = [
            [1, 0, 0, 1],
            [-1, 0, 0, 1],
            [0, 1, 0, 1],
            [0, -1, 0, 1],
            [0, 0, 1, 1],
            [0, 0, -1, 1],
        ]
        hull = hull_from_planes(ps)
        self.assertEqual(len(hull), 8)
        self.assertEqual(encode_collision([hull])[:8], b"MWCOLL02")

    def test_coplanar_collision_rejected(self):
        with self.assertRaises(ValueError):
            encode_collision([[[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0]]])

    def test_q3_real_geometry_fixture(self):
        s = read_q3(self.file("test.bsp", q3_fixture()))
        s.validate()
        self.assertEqual((s.stats["triangles"], len(s.hulls)), (1, 1))

    def test_q3_patch_is_curved_and_solid(self):
        s = read_q3(self.file("patch.bsp", q3_fixture(True)), 4)
        s.validate()
        self.assertEqual(s.stats["triangles"], 32)
        self.assertAlmostEqual(
            max(v["position"][2] for v in s.surfaces[0]["vertices"]), 4
        )
        self.assertEqual(len(s.hulls), 33)

    def test_q3_bad_directory(self):
        data = bytearray(q3_fixture())
        struct.pack_into("<i", data, 8, -1)
        with self.assertRaises(ValueError):
            BSP(self.file("bad.bsp", data))

    def test_q3_overlap(self):
        data = bytearray(q3_fixture())
        struct.pack_into("<i", data, 16, 144)
        with self.assertRaises(ValueError):
            BSP(self.file("bad.bsp", data))

    def test_q3_truncated(self):
        with self.assertRaises(ValueError):
            BSP(self.file("bad.bsp", q3_fixture()[:-1]))

    def test_wrong_engine_never_aliased(self):
        data = bytearray(q3_fixture())
        struct.pack_into("<i", data, 4, 22)
        with self.assertRaises(ValueError):
            detect(self.file("cod.bsp", data))

    def test_ql_header_selected(self):
        data = bytearray(q3_fixture())
        struct.pack_into("<i", data, 4, 47)
        self.assertEqual(read_q3(self.file("ql.bsp", data)).engine, "ql")

    def test_q3_bad_mesh_index(self):
        data = bytearray(q3_fixture())
        offset = struct.unpack_from("<i", data, 8 + 11 * 8)[0]
        struct.pack_into("<i", data, offset, 999)
        with self.assertRaises(ValueError):
            read_q3(self.file("bad.bsp", data))

    def test_obj_negative_indices_and_concave(self):
        data = "v 0 0 0\nv 3 0 0\nv 3 3 0\nv 1 1 0\nv 0 3 0\nf -5 -4 -3 -2 -1\n"
        scene = read_obj(self.file("mesh.obj", data))
        scene.validate()
        self.assertEqual(scene.stats["triangles"], 3)

    def test_obj_zero_index(self):
        with self.assertRaises(ValueError):
            read_obj(self.file("bad.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2"))

    def test_obj_zero_area_is_counted(self):
        s = read_obj(
            self.file("mesh.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 1 2\nf 1 2 3")
        )
        self.assertEqual(s.stats["degenerate_faces_removed"], 1)
        s.validate()
        self.assertEqual(s.stats["triangles"], 1)

    def test_obj_zero_normal_repair_is_counted(self):
        s = read_obj(
            self.file(
                "normals.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 0\nf 1//1 2//1 3//1"
            )
        )
        self.assertEqual(s.stats["zero_normals_rebuilt"], 3)
        s.validate()

    def test_obj_path_traversal(self):
        with self.assertRaises(ValueError):
            read_obj(self.file("bad.obj", "mtllib ../escape.mtl"))

    def test_obj_v_flipped(self):
        s = read_obj(
            self.file("uv.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt .1 .2\nf 1/1 2/1 3/1")
        )
        self.assertAlmostEqual(s.surfaces[0]["vertices"][0]["uv"][1], 0.8)

    def test_nonfinite_geometry(self):
        with self.assertRaises(ValueError):
            read_obj(self.file("bad.obj", "v nan 0 0"))

    def test_spawns_require_authored_location(self):
        with self.assertRaises(ValueError):
            spawns(Scene("obj"), [])

    def test_spawns_preserve_native_baseline(self):
        text, n = spawns(
            Scene(
                "q3",
                entities=[
                    {
                        "classname": "info_player_deathmatch",
                        "origin": "10 20 30",
                        "angle": "90",
                    }
                ],
            ),
            [],
        )
        self.assertEqual(n, 1)
        self.assertIn("script_model", text)
        self.assertIn('80 "0 90 0"', text)

    def test_native_winding_clockwise(self):
        s = read_q3(self.file("test.bsp", q3_fixture()))
        out = split_surfaces(s.surfaces, {"test": 0}, False)[0]
        a, b, c = [out["vertices"][j]["position"] for j in out["indices"]]
        self.assertLess(cross(sub(b, a), sub(c, a))[2], 0)

    def test_normal_packing_uses_replay_quaternion(self):
        sys.path.insert(0, str(ROOT.parent / "mw120rproxy/mw120rproxy/tools"))
        from replay_mesh_math import unpack_normal

        for n in ([1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, -1], [0.6, 0, 0.8]):
            actual = unpack_normal(packed_normal(n))
            self.assertGreater(dot(n, actual), 0.999)

    def test_zip_traversal_rejected(self):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as z:
            z.writestr("../escape.png", b"bad")
        with self.assertRaises(ValueError):
            Assets([self.file("bad.pk3", data.getvalue())], Scene("q3"))

    def test_zip_texture_case_insensitive(self):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as z:
            z.writestr("Textures/Test.txt", b"payload")
        a = Assets([self.file("good.pk3", data.getvalue())], Scene("q3"))
        self.assertEqual(a.read("textures/test.txt"), b"payload")

    def test_gltf_singular_matrix(self):
        with self.assertRaises(ValueError):
            node_matrix({"scale": [1, 0, 1]})

    def test_gltf_external_uri_containment(self):
        d = {
            "asset": {"version": "2.0"},
            "buffers": [{"byteLength": 4, "uri": "../escape.bin"}],
        }
        with self.assertRaises(ValueError):
            read_gltf(self.file("bad.gltf", json.dumps(d)))

    def test_glb_truncated_length(self):
        with self.assertRaises(ValueError):
            read_gltf(
                self.file("bad.glb", b"glTF" + struct.pack("<II", 2, 200) + b"12345678")
            )

    def test_gltf_reflected_nonuniform_transform(self):
        raw = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 1)
        d = {
            "asset": {"version": "2.0"},
            "buffers": [
                {
                    "byteLength": len(raw),
                    "uri": "data:application/octet-stream;base64,"
                    + base64.b64encode(raw).decode(),
                }
            ],
            "bufferViews": [{"buffer": 0, "byteLength": len(raw)}],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}
            ],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
            "nodes": [{"mesh": 0, "scale": [-2, 3, 4], "translation": [5, 6, 7]}],
            "scenes": [{"nodes": [0]}],
        }
        s = read_gltf(self.file("transform.gltf", json.dumps(d)))
        s.validate()
        surface = s.surfaces[0]
        self.assertEqual(surface["vertices"][0]["position"], [5, -7, 6])
        a, b, c = [surface["vertices"][i] for i in surface["indices"]]
        normal = cross(
            sub(b["position"], a["position"]), sub(c["position"], a["position"])
        )
        self.assertGreater(dot(normal, a["normal"]), 0)

    def test_native_oat_iw4_fixture(self):
        root = ROOT / "evidence/multi_engine_20260908/oat_fixtures/iw4"
        if not root.exists():
            self.skipTest("Build and run the native OAT export fixture first")
        s = read_cod(root, expected="iw4")
        s.validate()
        self.assertEqual((s.stats["triangles"], len(s.hulls)), (2, 1))

    def test_native_oat_iw5_fixture(self):
        root = ROOT / "evidence/multi_engine_20260908/oat_fixtures/iw5"
        if not root.exists():
            self.skipTest("Build and run the native OAT export fixture first")
        s = read_cod(root, expected="iw5")
        s.validate()
        self.assertEqual((s.stats["triangles"], len(s.hulls)), (2, 1))

    def test_oat_wrong_engine_rejected(self):
        root = ROOT / "evidence/multi_engine_20260908/oat_fixtures/iw4"
        if not root.exists():
            self.skipTest("Build and run the native OAT export fixture first")
        with self.assertRaises(ValueError):
            read_cod(root, expected="iw5")

    def test_failure_report_preserved(self):
        path = self.file("bad.bsp", b"IBSP" + struct.pack("<i", 999))
        output = self.root / "build"
        args = parser().parse_args(
            [str(path), "mp_bad", "-o", str(output), "--graybox", *REPLAY_ARGS]
        )
        with self.assertRaises(ValueError):
            run(args)
        self.assertEqual(
            json.loads((output / "report.json").read_text())["status"], "failed"
        )

    def test_existing_output_preserved(self):
        source = self.file("test.bsp", q3_fixture())
        output = self.root / "old"
        output.mkdir()
        marker = output / "user.txt"
        marker.write_text("keep")
        args = parser().parse_args(
            [str(source), "mp_bad", "-o", str(output), "--graybox", *REPLAY_ARGS]
        )
        with self.assertRaises(ValueError):
            run(args)
        self.assertEqual(marker.read_text(), "keep")

    def test_map_archive_selects_without_extracting_unrelated_content(self):
        if not REPLAY.is_file():
            self.skipTest("Set IW8_REPLAY_EXE to run native collision integration tests")
        raw = io.BytesIO()
        with zipfile.ZipFile(raw, "w") as z:
            z.writestr("maps/selected.bsp", q3_fixture())
            z.writestr("unused/readme.txt", "preserve in archive")
        source = self.file("map.pk3", raw.getvalue())
        output = self.root / "archive-import"
        args = parser().parse_args(
            [str(source), "mp_archive", "-o", str(output), "--graybox", *REPLAY_ARGS]
        )
        self.assertEqual(run(args), 0)
        self.assertFalse((output / "source-input/unused").exists())
        self.assertEqual(
            json.loads((output / "report.json").read_text())["archive_member"],
            "maps/selected.bsp",
        )

    def test_archive_requires_unambiguous_source_map(self):
        raw = io.BytesIO()
        with zipfile.ZipFile(raw, "w") as z:
            z.writestr("maps/a.bsp", q3_fixture())
            z.writestr("maps/b.bsp", q3_fixture())
        source = self.file("maps.pk3", raw.getvalue())
        output = self.root / "ambiguous"
        args = parser().parse_args(
            [str(source), "mp_archive", "-o", str(output), "--graybox", *REPLAY_ARGS]
        )
        with self.assertRaisesRegex(ValueError, "2 matching maps"):
            run(args)

    def test_native_argument_boundaries(self):
        if not REPLAY.is_file():
            self.skipTest("Set IW8_REPLAY_EXE to run native collision integration tests")
        writer = ROOT / "xmake-out/x64/Release/iw8-zonetool.exe"
        path = self.file("source with spaces.bsp", q3_fixture())
        output = self.root / "output with spaces"
        title = 'Quoted "map" & $(no shell)'
        credit = "Literal backslash \\ and spaces"
        result = subprocess.run(
            # Inspect the failure output explicitly below.
            [
                str(writer),
                "import",
                str(path),
                "mp_argv",
                "-o",
                str(output),
                "--graybox",
                "--title",
                title,
                "--credit",
                credit,
                *REPLAY_ARGS,
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        m = json.loads((output / "package/manifest.json").read_text())
        self.assertEqual(m["title"], title)
        self.assertIn(credit, m["description"])


@unittest.skipUnless(
    (DOWNLOADS / "q3_lobby.bsp").is_file(), "Run tools/fetch_import_samples.py first"
)
class DownloadedFixtureTests(unittest.TestCase):
    def test_q3_pinned_map(self):
        s = read_q3(DOWNLOADS / "q3_lobby.bsp")
        s.validate()
        self.assertEqual((s.stats["triangles"], len(s.hulls)), (12, 6))

    def test_q2_pinned_map(self):
        s = read_q2(DOWNLOADS / "q2_lobby.bsp")
        s.validate()
        self.assertEqual((s.stats["triangles"], len(s.hulls)), (52, 6))

    def test_source_solid_brushes_are_preserved(self):
        s = read_source(DOWNLOADS / "source_lobby.bsp")
        s.validate()
        self.assertEqual((s.stats["triangles"], len(s.hulls)), (32, 6))

    def test_source_displacements(self):
        s = read_source(DOWNLOADS / "source_displacement.bsp")
        s.validate()
        self.assertEqual((s.stats["triangles"], len(s.hulls)), (96, 102))

    def test_gltf_embedded_texture(self):
        s = read_gltf(DOWNLOADS / "box_textured.glb")
        s.validate()
        self.assertEqual(s.stats["triangles"], 12)
        self.assertTrue(s.materials["0"]["image_bytes"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
