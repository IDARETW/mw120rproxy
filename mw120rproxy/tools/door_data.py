"""Import authored brush-door pivots, motion, render poses and collision."""

import copy
import math
import re
import struct
from cod4_assets import rotation
from radiant_source import vector
from imported_map_assets import unpack_normal, packed_normal

STEPS = 24


def rotate(v, degrees):
    angle = math.radians(degrees)
    c, s = math.cos(angle), math.sin(angle)
    return [c * v[0] - s * v[1], s * v[0] + c * v[1], v[2]]


def point(v, door, phase):
    p = rotate([v[k] - door["pivot"][k] for k in range(3)], door["angle"] * phase)
    return [p[k] + door["pivot"][k] + door["travel"][k] * phase for k in range(3)]


def discover(entities, world, script):
    # Only read literal entity links and motions. CoD4 scripts are never executed.
    script = re.sub(r"/\*.*?\*/|//[^\n]*", "", script, flags=re.S)
    variables = dict(
        re.findall(r'(\w+)\s*=\s*getEnt\(\s*"([^"]+)"\s*,\s*"targetname"\s*\)', script, re.I)
    )
    links = {
        variables[a]: variables[b]
        for a, b in re.findall(r"(\w+)\s+linkto\(\s*(\w+)\s*\)", script, re.I)
        if a in variables and b in variables
    }
    motions = {}
    for var, kind, amount in re.findall(
        r"(\w+)\s+(rotateYaw|move[XYZ])\(\s*(-?[\d.]+)\s*,", script, re.I
    ):
        if var in variables:
            motions.setdefault(variables[var], []).append((kind.lower(), float(amount)))
    targets = {e["targetname"]: e for e in entities if e.get("targetname")}
    result = []
    for entity in entities:
        name = entity.get("targetname", "")
        if entity.get("classname") != "script_brushmodel" or "door" not in name.lower():
            continue
        model = entity.get("model", "")
        if not re.fullmatch(r"\*\d+", model):
            continue
        pivot_name = links.get(name, name)
        motion = motions.get(pivot_name, [])
        if not motion:
            continue
        kinds = {kind for kind, _ in motion}
        if len(kinds) != 1:
            raise ValueError(f"Door {name}: mixed motion requires an explicit import definition")
        kind = motion[0][0]
        amount = max((a for _, a in motion), key=lambda a: (abs(a), a))
        if not math.isfinite(amount) or amount == 0:
            raise ValueError(f"Door {name}: invalid travel")
        model_index = int(model[1:])
        origin = vector(entity.get("origin", "0 0 0"))
        axis = rotation(vector(entity.get("angles", "0 0 0")))
        lo, hi = world["brush_models"][model_index]["bounds"]
        corners = [
            [x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])
        ]
        corners = [
            [sum(axis[k][j] * v[j] for j in range(3)) + origin[k] for k in range(3)]
            for v in corners
        ]
        travel = [0.0] * 3
        if kind != "rotateyaw":
            travel["xyz".index(kind[-1])] = amount
        result.append(
            {
                "name": name,
                "model": model_index,
                "group": len(result),
                "pivot": vector(targets[pivot_name].get("origin", "0 0 0")),
                "angle": amount if kind == "rotateyaw" else 0.0,
                "travel": travel,
                "mins": [min(v[k] for v in corners) for k in range(3)],
                "maxs": [max(v[k] for v in corners) for k in range(3)],
                "duration": 0.9 if kind == "rotateyaw" else 3.0,
                "frames": 2 * STEPS + 1 if kind == "rotateyaw" else STEPS + 1,
                "hulls": [],
            }
        )
    if len(result) > 32:
        raise ValueError("Map exceeds 32 imported doors")
    # Adjacent leaves with the same numbered name operate as a double door.
    for i, a in enumerate(result):
        for b in result[:i]:
            if (
                a["angle"]
                and b["angle"]
                and re.sub(r"\d+$", "", a["name"]) == re.sub(r"\d+$", "", b["name"])
            ):
                if sum((a["pivot"][k] - b["pivot"][k]) ** 2 for k in range(3)) < 256**2:
                    a["group"] = b["group"]
    return result


def add_hull(door, brush, entity):
    planes = list(brush["planes"])
    for k in range(3):
        for sign, distance in ((1, brush["maxs"][k]), (-1, -brush["mins"][k])):
            n = [0.0] * 3
            n[k] = sign
            planes.append(n + [distance])
    axis = rotation(vector(entity.get("angles", "0 0 0")))
    origin = vector(entity.get("origin", "0 0 0"))
    transformed = []
    for p in planes:
        length = math.sqrt(sum(x * x for x in p[:3]))
        if length < 1e-6:
            raise ValueError("Degenerate door collision plane")
        n = [sum(axis[k][j] * p[j] for j in range(3)) / length for k in range(3)]
        transformed.append(n + [p[3] / length + sum(n[k] * origin[k] for k in range(3))])
    door["hulls"].append(transformed)


def poses(surfaces, doors):
    output = []
    for surface in surfaces:
        if "door" not in surface:
            output.append(surface)
            continue
        door = doors[surface["door"]]
        for frame in range(door["frames"]):
            phase = frame / STEPS - (1 if door["angle"] else 0)
            s = copy.deepcopy(surface)
            s["doorFrame"] = frame
            for v in s["vertices"]:
                v["position"] = point(v["position"], door, phase)
                v["normal"] = packed_normal(
                    rotate(unpack_normal(v["normal"]), door["angle"] * phase)
                )
            output.append(s)
    return output


def encode(doors, surfaces):
    data = bytearray(b"MWRDOR01" + struct.pack("<II", len(doors), len(surfaces)))
    used = set()
    for index, door in enumerate(doors):
        name = door["name"].encode("ascii")
        if not 0 < len(name) < 64 or not door["hulls"]:
            raise ValueError(f"Door {door['name']}: missing collision or invalid name")
        data += struct.pack(
            "<64sII14fI",
            name,
            door["group"],
            door["frames"],
            *door["pivot"],
            door["angle"],
            *door["travel"],
            *door["mins"],
            *door["maxs"],
            door["duration"],
            len(door["hulls"]),
        )
        for hull in door["hulls"]:
            data += struct.pack("<I", len(hull))
            for plane in hull:
                data += struct.pack("<4f", *plane)
        for frame in range(door["frames"]):
            ids = [
                i
                for i, s in enumerate(surfaces)
                if s.get("door") == index and s.get("doorFrame") == frame
            ]
            if not ids or any(i in used for i in ids):
                raise ValueError(f"Door {door['name']}: missing or shared render pose")
            used.update(ids)
            data += struct.pack("<I", len(ids)) + struct.pack(f"<{len(ids)}I", *ids)
    validate(data)
    return bytes(data)


def validate(data):
    if not 16 <= len(data) <= 16 * 1024 * 1024 or data[:8] != b"MWRDOR01":
        raise ValueError("Invalid door sidecar header")
    offset = 8

    def read(fmt):
        nonlocal offset
        size = struct.calcsize(fmt)
        if offset + size > len(data):
            raise ValueError("Truncated door sidecar")
        result = struct.unpack_from(fmt, data, offset)
        offset += size
        return result

    count, surfaces = read("<II")
    if count > 32 or not 0 < surfaces <= 4096:
        raise ValueError("Door count or surface budget exceeded")
    used = set()
    hull_count = 0
    for _ in range(count):
        name, group, frames, *values = read("<64sII14fI")
        hulls = values.pop()
        if not name[0] or name[-1] or group >= count or not 1 <= hulls <= 128:
            raise ValueError("Invalid door record")
        if not all(math.isfinite(v) and abs(v) <= 100000 for v in values):
            raise ValueError("Nonfinite or out-of-range door coordinate")
        angle = values[3]
        if frames != (49 if angle else 25) or abs(angle) > 170 or not 0.1 <= values[13] <= 10:
            raise ValueError("Invalid door motion")
        if not angle and sum(v * v for v in values[4:7]) < 1:
            raise ValueError("Sliding door has no travel")
        if any(values[7 + k] >= values[10 + k] for k in range(3)):
            raise ValueError("Invalid door bounds")
        for _ in range(hulls):
            (planes,) = read("<I")
            if not 4 <= planes <= 70:
                raise ValueError("Invalid door collision plane count")
            for _ in range(planes):
                plane = read("<4f")
                if (
                    not all(math.isfinite(v) and abs(v) <= 200000 for v in plane)
                    or abs(sum(v * v for v in plane[:3]) - 1) > 0.01
                ):
                    raise ValueError("Invalid door collision plane")
        hull_count += hulls
        for _ in range(frames):
            (size,) = read("<I")
            if not 1 <= size <= surfaces:
                raise ValueError("Invalid door render pose")
            for index in read(f"<{size}I"):
                if index >= surfaces or index in used:
                    raise ValueError("Shared or out-of-range door surface")
                used.add(index)
    if offset != len(data):
        raise ValueError("Trailing door sidecar bytes")
    return {"doors": count, "surfaces": surfaces, "collision_hulls": hull_count}


if __name__ == "__main__":
    import argparse
    import json
    from pathlib import Path

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(validate(args.validate.read_bytes()), indent=2))
