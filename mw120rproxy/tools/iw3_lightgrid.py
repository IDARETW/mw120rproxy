"""Read exported IW3 light-grid cells without discarding their directional palette."""

import argparse
import hashlib
import json
import re
from pathlib import Path, PurePosixPath
import struct


def read_grid(root, mapid):
    root = Path(root).resolve()
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", mapid):
        raise ValueError("Invalid map id")
    meta = json.loads((root / f"maps/mp/{mapid}.d3dbsp.lightgrid.json").read_text())
    if meta.get("schema") != 1 or meta.get("name") != f"maps/mp/{mapid}.d3dbsp":
        raise ValueError("Mismatched light-grid export")
    if meta.get("available") is False:
        raise ValueError("The source map has no compiled light grid")
    row_axis, col_axis = meta["row_axis"], meta["col_axis"]
    if {row_axis, col_axis} != {0, 1}:
        raise ValueError("Unsupported light-grid axes")
    mins, maxs = meta["mins"], meta["maxs"]
    if (
        len(mins) != 3
        or len(maxs) != 3
        or any(
            type(a) is not int or type(b) is not int or not 0 <= a <= b <= 65535
            for a, b in zip(mins, maxs)
        )
    ):
        raise ValueError("Invalid grid bounds")
    rows = maxs[row_axis] - mins[row_axis] + 1
    entries, colors = meta["entry_count"], meta["color_count"]
    if meta["row_count"] != rows or not 0 <= entries <= 8388608 or not 0 <= colors <= 65536:
        raise ValueError("Invalid grid counts")

    def array(key, size=None):
        desc = meta[key]
        relative = PurePosixPath(desc["file"])
        if (
            relative.is_absolute()
            or ".." in relative.parts
            or "\\" in desc["file"]
            or ":" in desc["file"]
        ):
            raise ValueError("Unsafe grid array path")
        path = root.joinpath(*relative.parts).resolve()
        if not path.is_relative_to(root) or not 0 <= desc["bytes"] <= 64 * 1024 * 1024:
            raise ValueError("Invalid grid array")
        if path.stat().st_size != desc["bytes"] or (size is not None and size != desc["bytes"]):
            raise ValueError("Incorrect grid array length")
        return path.read_bytes()

    starts = struct.unpack(f"<{rows}H", array("row_starts", rows * 2))
    raw = array("row_data")
    data = list(struct.iter_unpack("<HBB", array("entries", entries * 4)))
    palette = array("colors", colors * 168)
    if any(c >= colors for c, _, _ in data):
        raise ValueError("Grid entry references a missing palette color")
    cells = []
    expected_entry = 0
    previous_end = 0
    for row, offset in enumerate(starts):
        if offset == 65535:
            continue
        offset *= 4
        if offset < previous_end or offset + 12 > len(raw):
            raise ValueError("Overlapping or truncated grid row")
        col, width, bottom, height, first = struct.unpack_from("<4HI", raw, offset)
        if first != expected_entry or not width or not height:
            raise ValueError("Invalid grid row entry range")
        if (
            col < mins[col_axis]
            or col + width - 1 > maxs[col_axis]
            or bottom < mins[2]
            or bottom + height - 1 > maxs[2]
        ):
            raise ValueError("Grid row exceeds bounds")
        cursor, consumed = offset + 12, 0
        while consumed < width:
            if cursor + 2 > len(raw):
                raise ValueError("Truncated grid column run")
            run, count = raw[cursor : cursor + 2]
            cursor += 2
            if not run or consumed + run > width:
                raise ValueError("Invalid grid column run")
            start_z = 0
            if count:
                if cursor == len(raw):
                    raise ValueError("Missing grid column height")
                start_z = raw[cursor]
                cursor += 1
                if start_z + count > height or expected_entry + run * count > entries:
                    raise ValueError("Grid column exceeds its row or entries")
            for column in range(run):
                for z in range(count):
                    coord = [0, 0, bottom + start_z + z]
                    coord[row_axis] = mins[row_axis] + row
                    coord[col_axis] = col + consumed + column
                    position = [
                        coord[0] * 32 - 131072,
                        coord[1] * 32 - 131072,
                        coord[2] * 64 - 131072,
                    ]
                    cells.append((*position, *data[expected_entry]))
                    expected_entry += 1
            consumed += run
        previous_end = (cursor + 3) & ~3
        if any(raw[cursor:previous_end]):
            raise ValueError("Nonzero grid row padding")
    if expected_entry != entries or previous_end != len(raw):
        raise ValueError("Unconsumed light-grid data")
    return meta, cells, palette


def export_positions(root, mapid, output):
    meta, cells, palette = read_grid(root, mapid)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    positions = b"".join(struct.pack("<3fHBB", *cell) for cell in cells)
    (output / "iw3_probe_cells.bin").write_bytes(positions)
    (output / "iw3_probe_palette.bin").write_bytes(palette)
    report = {
        "map": mapid,
        "cells": len(cells),
        "palette_colors": meta["color_count"],
        "directional_samples_per_color": 56,
        "cell_record": "float32 xyz, uint16 palette index, uint8 primary light, uint8 trace mask",
        "cell_sha256": hashlib.sha256(positions).hexdigest(),
        "palette_sha256": hashlib.sha256(palette).hexdigest(),
        "runtime_format": False,
    }
    (output / "lightgrid_report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, required=True)
    parser.add_argument("--map", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(export_positions(args.dump, args.map, args.out), indent=2))
