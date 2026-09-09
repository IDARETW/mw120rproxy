import copy
import json
import struct
import tempfile
import unittest
from pathlib import Path
import ladder_data
from imported_map_assets import read_material
from glass_material import create

ROOT = Path(__file__).resolve().parents[2]


class MapFeaturesTests(unittest.TestCase):
    def test_office_authored_ladder_faces(self):
        dump = json.loads(
            (
                ROOT
                / "custom_map_sources/mp_4doffice/extracted/maps/mp/mp_4doffice.d3dbsp.replay-collision.json"
            ).read_text()
        )
        faces = [f for b in dump["brushes"] for f in ladder_data.faces(b)]
        self.assertEqual(len(faces), 18)
        self.assertEqual(ladder_data.validate(ladder_data.encode(faces)), 18)
        self.assertTrue(all((f["top"][2] - f["bottom"][2]) % 12 == 0 for f in faces))
        for a, b in zip(faces[::2], faces[1::2]):
            self.assertEqual(a["normal"], [-v for v in b["normal"]])

    def test_reject_bad_ladder_payload(self):
        face = {"bottom": [0, 0, 0], "top": [0, 0, 120], "normal": [1, 0, 0], "width": 32}
        good = ladder_data.encode([face])
        for bad in (
            good[:-1],
            good + b"X",
            good[:12] + struct.pack("<f", float("nan")) + good[16:],
        ):
            with self.assertRaises(ValueError):
                ladder_data.validate(bad)
        invalid = copy.deepcopy(face)
        invalid["top"][0] = 5
        with self.assertRaises(ValueError):
            ladder_data.encode([invalid])

    def test_nuketown_glass_keeps_alpha_and_foliage_keeps_cutout(self):
        root = ROOT / "custom_map_sources/mp_nuketown/extracted"
        for name in ("mc/com_glass_clear", "mc/mtl_80s_econ_glass_outside"):
            m = read_material(root, name)
            self.assertTrue(m["blended"])
            self.assertFalse(m["alpha_test"])
            self.assertNotIn("skip", m)
        self.assertTrue(read_material(root, "mc/mtl_perc_doubletap")["alpha_test"])

    def test_glass_technique_has_no_prepass_or_shadows(self):
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
            create(folder, stem, "mp_fixture")
            ts = json.loads((folder / (stem + ".glass.techset.json")).read_text())
            flags = struct.unpack_from("<Q", bytes.fromhex(ts["header"]), 8)[0]
            self.assertEqual(flags & 0x80A1, 1)
            self.assertEqual(len(ts["techniques"]), 1)
            header = bytes.fromhex(ts["techniques"][0]["header"])
            self.assertEqual(struct.unpack_from("<I", header, 8)[0], 34)
            self.assertEqual(struct.unpack_from("<QQ", header, 0xA0), (0xC00, 0x28054))
            self.assertEqual(struct.unpack_from("<Q", bytes.fromhex(ts["header"]), 24)[0], 1 << 34)
            create(folder, stem, "mp_fixture", "foliage")
            foliage = json.loads((folder / (stem + ".foliage.techset.json")).read_text())
            flags = struct.unpack_from("<Q", bytes.fromhex(foliage["header"]), 8)[0]
            self.assertEqual(flags & 0x80A1, 0xA1)


if __name__ == "__main__":
    unittest.main()
