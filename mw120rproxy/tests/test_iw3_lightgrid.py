import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from iw3_lightgrid import read_grid


class GridTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "maps/mp/mp_test.d3dbsp.lightgrid.json"
        self.path.parent.mkdir(parents=True)
        # Two populated columns separated by an empty run, then a missing row.
        raw = struct.pack("<4HI", 4096, 3, 2048, 3, 0) + bytes([1, 2, 0, 1, 0, 1, 1, 2])
        self.meta = dict(
            schema=1,
            name="maps/mp/mp_test.d3dbsp",
            row_axis=0,
            col_axis=1,
            mins=[4096, 4096, 2048],
            maxs=[4097, 4098, 2050],
            row_count=2,
            entry_count=3,
            color_count=2,
        )
        for key, data in dict(
            row_starts=struct.pack("<2H", 0, 65535),
            row_data=raw,
            entries=struct.pack("<HBBHBBHBB", 0, 1, 0, 1, 0, 85, 1, 1, 170),
            colors=bytes(168) + bytes([255]) * 168,
        ).items():
            (self.root / (key + ".bin")).write_bytes(data)
            self.meta[key] = dict(file=key + ".bin", bytes=len(data))
        self.save()

    def save(self):
        self.path.write_text(json.dumps(self.meta))

    def test_positions_preserve_entries_and_trace_masks(self):
        _, cells, palette = read_grid(self.root, "mp_test")
        self.assertEqual(cells, [(0, 0, 0, 0, 1, 0), (0, 0, 64, 1, 0, 85), (0, 64, 128, 1, 1, 170)])
        self.assertEqual(palette, bytes(168) + bytes([255]) * 168)

    def test_row_axis_swap(self):
        self.meta.update(row_axis=1, col_axis=0, maxs=[4098, 4097, 2050])
        self.save()
        self.assertEqual(read_grid(self.root, "mp_test")[1][-1][:3], (64, 0, 128))

    def test_reject_bad_runs_and_palette_indices(self):
        original = (self.root / "row_data.bin").read_bytes()
        for position, value in [(12, 0), (12, 4), (13, 4), (14, 3)]:
            with self.subTest(position=position, value=value):
                data = bytearray(original)
                data[position] = value
                (self.root / "row_data.bin").write_bytes(data)
                with self.assertRaises(ValueError):
                    read_grid(self.root, "mp_test")
        (self.root / "row_data.bin").write_bytes(original)
        entries = bytearray((self.root / "entries.bin").read_bytes())
        struct.pack_into("<H", entries, 0, 2)
        (self.root / "entries.bin").write_bytes(entries)
        with self.assertRaises(ValueError):
            read_grid(self.root, "mp_test")

    def test_reject_overlap_truncation_and_paths(self):
        original = copy.deepcopy(self.meta)
        for key, field, value in [("row_data", "bytes", 19), ("colors", "file", "../outside.bin")]:
            self.meta = copy.deepcopy(original)
            self.meta[key][field] = value
            self.save()
            with self.assertRaises(ValueError):
                read_grid(self.root, "mp_test")
        self.meta = original
        self.save()
        (self.root / "row_starts.bin").write_bytes(bytes(4))
        with self.assertRaises(ValueError):
            read_grid(self.root, "mp_test")


if __name__ == "__main__":
    unittest.main()
