"""Build a spatial ambient sidecar from the source map's compiled light grid."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from iw3_lightgrid import read_grid

MAGIC = b"MWLGRID1"
MAX_CELLS = 2_000_000


def validate_grid(path):
    """Check the exact runtime sidecar contract without loading the game."""
    path = Path(path)
    size = path.stat().st_size
    if not 16 <= size <= 16 + MAX_CELLS * 16 + 65536 * 12:
        raise ValueError("Invalid spatial ambient byte count")
    data = path.read_bytes()
    if data[:8] != MAGIC:
        raise ValueError("Invalid spatial ambient magic")
    count, palette_count = struct.unpack_from("<II", data, 8)
    if not 1 <= count <= MAX_CELLS or not 1 <= palette_count <= 65536:
        raise ValueError("Invalid spatial ambient counts")
    if len(data) != 16 + count * 16 + palette_count * 12:
        raise ValueError("Invalid spatial ambient byte count")
    previous = None
    for x, y, z, color, _light, _trace in struct.iter_unpack("<3iHBB", data[16 : 16 + count * 16]):
        coordinate = (x, y, z)
        if previous is not None and coordinate <= previous:
            raise ValueError("Spatial ambient cells must have unique sorted coordinates")
        if color >= palette_count:
            raise ValueError("Spatial ambient cell references a missing palette color")
        if any(abs(value) * step > 100000 for value, step in zip(coordinate, (32, 32, 64))):
            raise ValueError("Spatial ambient cell exceeds the supported world")
        previous = coordinate
    for color in struct.iter_unpack("<3f", data[16 + count * 16 :]):
        if any(not math.isfinite(value) or not 0 <= value <= 8 for value in color):
            raise ValueError("Invalid spatial ambient irradiance")
    return {
        "cells": count,
        "palette_colors": palette_count,
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def build_grid(dump, mapid, ambient, output):
    """Preserve spatial irradiance variation without inventing Replay grid records.

    Only the DC average of IW3's directional palette is translated. Its scale is
    calibrated to the existing package ambient at the occupied-cell 90th
    percentile. Full directional SH conversion requires the source sample basis.
    """
    meta, cells, palette = read_grid(dump, mapid)
    if not cells or len(cells) > MAX_CELLS:
        raise ValueError("Unsupported spatial ambient cell count")
    ambient = Path(ambient).read_bytes()
    if len(ambient) != 72 or ambient[:8] != b"MWRAMB01":
        raise ValueError("Invalid ambient probe")
    fill = [struct.unpack_from("<e", ambient, 8 + k * 18)[0] * 0.886226925 for k in range(3)]
    if any(not math.isfinite(v) or not 0 <= v <= 8 for v in fill):
        raise ValueError("Invalid ambient probe irradiance")
    colors = [
        [sum(palette[i + k : i + 168 : 3]) / (56 * 255) for k in range(3)]
        for i in range(0, len(palette), 168)
    ]
    luma = [sum(a * b for a, b in zip(color, (0.2126, 0.7152, 0.0722))) for color in colors]
    occupied = sorted(luma[c[3]] for c in cells)
    reference = occupied[min(len(occupied) - 1, int((len(occupied) - 1) * 0.9))]
    target = sum(a * b for a, b in zip(fill, (0.2126, 0.7152, 0.0722)))
    gain = target / reference if reference > 1e-6 else 0
    colors = [[min(8, v * gain) for v in color] for color in colors]
    records = []
    for x, y, z, color, light, trace in cells:
        coordinate = (x // 32, y // 32, z // 64)
        if any(abs(p) > 100000 for p in (x, y, z)):
            raise ValueError("Spatial ambient cell exceeds the supported world")
        records.append((*coordinate, color, light, trace))
    records.sort(key=lambda c: c[:3])
    if any(a[:3] == b[:3] for a, b in zip(records, records[1:])):
        raise ValueError("Duplicate spatial ambient cells")
    data = MAGIC + struct.pack("<II", len(records), len(colors))
    data += b"".join(struct.pack("<3iHBB", *cell) for cell in records)
    data += b"".join(struct.pack("<3f", *color) for color in colors)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    validate_grid(output)
    report = {
        "schema": 1,
        "map": mapid,
        "cells": len(records),
        "palette_colors": len(colors),
        "source_sun_primary_light": meta.get("sun_primary_light_index"),
        "source_reference_luminance": reference,
        "irradiance_gain": gain,
        "source_palette_sha256": hashlib.sha256(palette).hexdigest(),
        "sidecar_sha256": hashlib.sha256(data).hexdigest(),
        "source_directional_samples": 56,
        "translated_coefficients": "isotropic DC average only",
        "runtime_scope": "local-player fallback probe; not a native per-object GPU grid",
        "sampling": "local trilinear cells with collision visibility; bounded nearest-visible fallback",
    }
    output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, required=True)
    parser.add_argument("--map", required=True)
    parser.add_argument("--ambient", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build_grid(args.dump, args.map, args.ambient, args.out), indent=2))
