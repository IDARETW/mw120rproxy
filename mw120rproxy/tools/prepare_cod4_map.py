"""Extract CoD4 Mod Tools map inputs without launching either game.

This is source preparation, not an IW8 collision/render converter. Preserve every
BSP lump and export the compiled entities plus geometry for the next conversion
stage. Never label the triangle-collision lump as the entire brush collision world.

Format references (independent implementation; no dependency on these projects):
https://github.com/snake-biscuits/bsp_tool/blob/master/bsp_tool/infinity_ward.py
https://github.com/snake-biscuits/bsp_tool/blob/master/bsp_tool/branches/infinity_ward/modern_warfare.py
https://github.com/snake-biscuits/bsp_tool/blob/master/bsp_tool/branches/infinity_ward/call_of_duty2.py
"""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_bsp(data):
    if len(data) < 12 or data[:4] != b"IBSP":
        raise ValueError("Not a CoD4 BSP")
    version, count = struct.unpack_from("<II", data, 4)
    if version != 22 or not 1 <= count <= 64 or 12 + count * 8 > len(data):
        raise ValueError("Expected a complete CoD4 version-22 lump directory")
    cursor = 12 + count * 8
    lumps, entries = {}, []
    for i in range(count):
        kind, size = struct.unpack_from("<II", data, 12 + i * 8)
        if kind in lumps:
            raise ValueError(f"Duplicate lump {kind:#x}")
        # Every PC CoD4Map lump starts on a four-byte boundary, including 7.
        # Odd-sized lump 6 exposes the alignment; skipping it shifts all later
        # vertices/entities and leaves an apparent extra word at EOF.
        cursor = (cursor + 3) & ~3
        if cursor + size > len(data):
            raise ValueError(f"Lump {kind:#x} extends beyond the BSP")
        body = data[cursor : cursor + size]
        lumps[kind] = body
        entries.append(dict(id=kind, offset=cursor, size=size, sha256=digest(body)))
        cursor += size
    if data[cursor:].strip(b"\0") or len(data) - cursor > 3:
        raise ValueError("Unexpected bytes after the last BSP lump")
    return lumps, entries


def records(data, fmt):
    if len(data) % struct.calcsize(fmt):
        raise ValueError(f"Partial {fmt} record")
    return list(struct.iter_unpack(fmt, data))


def finite(values):
    if any(not math.isfinite(v) for v in values):
        raise ValueError("Non-finite coordinate in source geometry")
    return list(values)


def geometry(lumps):
    result = {}
    for label, ids in [("layered", (9, 10, 11)), ("simple", (47, 48, 49))]:
        if not any(i in lumps for i in ids):
            continue
        if not all(i in lumps for i in ids):
            raise ValueError(f"Incomplete {label} geometry family")
        vertices = []
        for v in records(lumps[ids[1]], "<6f4B10f"):
            vertices.append(
                dict(
                    position=finite(v[:3]),
                    normal=finite(v[3:6]),
                    rgba=list(v[6:10]),
                    uv=finite(v[10:12]),
                    lightmap_uv=finite(v[12:14]),
                    remaining_floats=finite(v[14:]),
                )
            )
        indices = [v[0] for v in records(lumps[ids[2]], "<H")]
        soups = []
        for raw, first_vertex, num_vertices, num_indices, first_index in records(
            lumps[ids[0]], "<12sIHHI"
        ):
            if (
                first_vertex + num_vertices > len(vertices)
                or first_index + num_indices > len(indices)
                or num_indices % 3
            ):
                raise ValueError(f"Invalid {label} surface range")
            local = indices[first_index : first_index + num_indices]
            if any(i >= num_vertices for i in local):
                raise ValueError(f"Invalid {label} local vertex index")
            soups.append(
                dict(
                    source_metadata_hex=raw.hex(),
                    first_vertex=first_vertex,
                    num_vertices=num_vertices,
                    first_index=first_index,
                    triangles=[
                        [first_vertex + j for j in local[i : i + 3]]
                        for i in range(0, len(local), 3)
                    ],
                )
            )
        result[label] = dict(vertices=vertices, surfaces=soups)
    collision_vertices = [finite(v) for v in records(lumps.get(31, b""), "<3f")]
    collision_triangles = records(lumps.get(32, b""), "<3H")
    if any(i >= len(collision_vertices) for tri in collision_triangles for i in tri):
        raise ValueError("Invalid collision vertex index")
    result["triangle_collision"] = dict(
        vertices=collision_vertices,
        triangles=collision_triangles,
        includes_brush_collision=False,
        havok_cooked=False,
    )
    model_data = lumps.get(37, b"")
    if not model_data or len(model_data) % 48:
        raise ValueError("Missing or malformed BSP world model")
    bounds = finite(struct.unpack_from("<6f", model_data))
    if any(bounds[i] > bounds[i + 3] for i in range(3)):
        raise ValueError("Inverted world bounds")
    result["world_bounds"] = dict(min=bounds[:3], max=bounds[3:])
    result["native_iw8_assets_emitted"] = False
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mod-tools", type=Path, required=True)
    ap.add_argument("--map", default="mp_test")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", args.map):
        ap.error("Use a lower-case mp_<name> map id")
    root, out = args.mod_tools.resolve(), args.out.resolve()
    if out.exists():
        ap.error("Output already exists; use a new directory to preserve earlier work")
    if out == root or root in out.parents:
        ap.error("Keep extracted output outside the original Mod Tools tree")
    map_id = args.map
    files = [
        f"map_source/{map_id}.map",
        f"raw/maps/mp/{map_id}.d3dbsp",
        f"raw/maps/mp/{map_id}.gsc",
        f"zone/english/{map_id}.ff",
        f"zone/english/{map_id}_load.ff",
    ]
    sources = []
    for relative in files:
        path = root / relative
        data = path.read_bytes()
        sources.append(
            dict(path=str(path), relative_path=relative, size=len(data), sha256=digest(data))
        )
    bsp = (root / files[1]).read_bytes()
    lumps, entries = read_bsp(bsp)
    geo = geometry(lumps)
    raw_entities = lumps.get(39, b"").rstrip(b"\0")
    text = raw_entities.decode("ascii")
    if not text.lstrip().startswith("{") or not text.rstrip().endswith("}"):
        raise ValueError("BSP entity lump is not a complete entity string")
    classes = Counter(re.findall(r'"classname"\s+"([^"]+)"', text))
    if classes["worldspawn"] != 1:
        raise ValueError("Expected exactly one worldspawn")
    # All parsing succeeds before creating output. Existing output is never replaced.
    out.mkdir(parents=True)
    for entry in sources:
        destination = out / "original" / entry["relative_path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry["path"], destination)
        if digest(destination.read_bytes()) != entry["sha256"]:
            raise ValueError(f"Source-copy hash mismatch: {destination}")
    lump_root = out / "bsp_lumps"
    lump_root.mkdir()
    for kind, data in lumps.items():
        (lump_root / f"{kind:02x}.bin").write_bytes(data)
    # The existing converter accepts .ents alone. No fabricated ZoneTool .colmap.
    ents_path = out / "dump" / "maps" / "mp" / f"{map_id}.d3dbsp.ents"
    ents_path.parent.mkdir(parents=True)
    ents_path.write_bytes(raw_entities)
    (out / "geometry.json").write_text(json.dumps(geo, indent=2), encoding="utf-8")
    obj = ["# CoD4 compiled world geometry; materials unresolved; this is not an IW8 asset"]
    offset = 1
    for name in ("layered", "simple"):
        if name not in geo:
            continue
        group = geo[name]
        obj.append("o " + name)
        for v in group["vertices"]:
            obj.append("v " + " ".join(format(x, ".9g") for x in v["position"]))
        for surface in group["surfaces"]:
            for tri in surface["triangles"]:
                obj.append("f " + " ".join(str(i + offset) for i in tri))
        offset += len(group["vertices"])
    (out / "world.obj").write_text("\n".join(obj) + "\n", encoding="ascii")
    report = dict(
        schema=1,
        map=map_id,
        sources=sources,
        lump_count=len(entries),
        lumps=entries,
        entity_classes=dict(sorted(classes.items())),
        entity_count=sum(classes.values()),
        geometry={
            name: dict(
                vertices=len(geo[name]["vertices"]),
                surfaces=len(geo[name]["surfaces"]),
                triangles=sum(len(s["triangles"]) for s in geo[name]["surfaces"]),
            )
            for name in ("layered", "simple")
            if name in geo
        },
        triangle_collision=dict(
            vertices=len(geo["triangle_collision"]["vertices"]),
            triangles=len(geo["triangle_collision"]["triangles"]),
            includes_brush_collision=False,
        ),
        world_bounds=geo["world_bounds"],
        native_iw8_assets_emitted=False,
        game_launched=False,
        remaining=[
            "Replay fastfile acceptance and asset layouts",
            "IW8 world rendering and material conversion",
            "Havok collision including brush solids",
            "IW8 spawn classes and map scripts",
            "live match",
        ],
    )
    (out / "source_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k not in ("sources", "lumps")}, indent=2))


if __name__ == "__main__":
    main()
