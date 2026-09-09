import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from map_presentation import ambient_bytes
from spatial_ambient import build_grid, validate_grid


class SpatialAmbientTests(unittest.TestCase):
    def test_source_dark_room_and_bright_exterior_remain_distinct(self):
        cells = [(0, 0, 0, 0, 1, 85)] + [(32 * i, 0, 0, 1, 1, 0) for i in range(1, 10)]
        palette = bytes([32]) * 168 + bytes([128]) * 168
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "ambient.bin").write_bytes(ambient_bytes([2, 2, 2]))
            with patch("spatial_ambient.read_grid", return_value=({}, cells, palette)):
                report = build_grid(
                    root, "mp_test", root / "ambient.bin", root / "ambient_grid.bin"
                )
            data = (root / "ambient_grid.bin").read_bytes()
            self.assertEqual(data[:8], b"MWLGRID1")
            self.assertEqual(struct.unpack_from("<II", data, 8), (10, 2))
            self.assertEqual(struct.unpack_from("<3iHBB", data, 16), (0, 0, 0, 0, 1, 85))
            colors = list(struct.iter_unpack("<3f", data[16 + 10 * 16 :]))
            self.assertAlmostEqual(colors[1][0] / colors[0][0], 4)
            self.assertAlmostEqual(colors[1][0], 2, delta=0.001)
            self.assertEqual(report["cells"], 10)
            self.assertEqual(json.loads((root / "ambient_grid.json").read_text()), report)
            validated = validate_grid(root / "ambient_grid.bin")
            self.assertEqual(validated["cells"], 10)
            self.assertEqual(validated["palette_colors"], 2)
            self.assertEqual(validated["sha256"], report["sidecar_sha256"])

    def test_black_authored_grid_stays_black(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "ambient.bin").write_bytes(ambient_bytes([2, 2, 2]))
            with patch(
                "spatial_ambient.read_grid", return_value=({}, [(0, 0, 0, 0, 0, 0)], bytes(168))
            ):
                report = build_grid(root, "mp_test", root / "ambient.bin", root / "grid.bin")
            self.assertEqual(report["irradiance_gain"], 0)
            self.assertEqual(
                struct.unpack_from("<3f", (root / "grid.bin").read_bytes(), 32), (0, 0, 0)
            )

    def test_duplicate_cells_and_malformed_fallback_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fallback = root / "ambient.bin"
            fallback.write_bytes(ambient_bytes([2, 2, 2]))
            cells = [(0, 0, 0, 0, 0, 0)] * 2
            with patch("spatial_ambient.read_grid", return_value=({}, cells, bytes(168))):
                with self.assertRaisesRegex(ValueError, "Duplicate"):
                    build_grid(root, "mp_test", fallback, root / "grid.bin")
                fallback.write_bytes(b"broken")
                with self.assertRaisesRegex(ValueError, "ambient probe"):
                    build_grid(root, "mp_test", fallback, root / "grid.bin")

    def test_runtime_sidecar_rejects_invalid_records(self):
        valid = (
            b"MWLGRID1"
            + struct.pack("<II", 2, 1)
            + struct.pack("<3iHBB", -1, 0, 0, 0, 1, 85)
            + struct.pack("<3iHBB", 0, 0, 0, 0, 1, 85)
            + struct.pack("<3f", 0.5, 1, 2)
        )
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ambient_grid.bin"
            for label, offset, replacement in [
                ("magic", 0, b"BROKEN01"),
                ("palette", 28, struct.pack("<H", 1)),
                ("order", 32, struct.pack("<i", -1)),
                ("bounds", 16, struct.pack("<i", -2147483648)),
                ("negative", 48, struct.pack("<f", -1)),
                ("nonfinite", 48, struct.pack("<f", float("nan"))),
                ("huge", 8, struct.pack("<I", 2000001)),
            ]:
                with self.subTest(label=label):
                    bad = bytearray(valid)
                    bad[offset : offset + len(replacement)] = replacement
                    path.write_bytes(bad)
                    with self.assertRaises(ValueError):
                        validate_grid(path)
            for data in (valid[:-1], valid + b"\x00"):
                path.write_bytes(data)
                with self.assertRaisesRegex(ValueError, "byte count"):
                    validate_grid(path)


if __name__ == "__main__":
    unittest.main()
