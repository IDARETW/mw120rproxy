"""Strict source-map and portable box collision helpers for the Replay authoring path."""

import math
import re
import struct


def blocks(text):
    # Return immediate brace blocks, ignoring comments and quoted entity values.
    depth = 0
    start = None
    result = []
    for match in re.finditer(r'//[^\n]*|"(?:\\.|[^"\\])*"|[{}]', text):
        token = match.group()
        if token == "{":
            if depth == 0:
                start = match.start()
            depth += 1
        elif token == "}":
            depth -= 1
            if depth < 0:
                raise ValueError("Unbalanced map braces")
            if depth == 0:
                result.append(text[start : match.end()])
    if depth:
        raise ValueError("Unclosed map entity or brush")
    return result


def properties(block):
    # Entity properties precede brush data. Never read a nested brush as keys.
    body = block[1:-1]
    children = blocks(body)
    if children:
        body = body[: body.index(children[0])]
    return dict(re.findall(r'"([^"\n]+)"\s+"([^"\n]*)"', body))


def vector(text):
    values = list(map(float, text.split()))
    if len(values) != 3 or any(not math.isfinite(x) or abs(x) > 100000 for x in values):
        raise ValueError(f"Invalid three-component coordinate: {text}")
    return values


def collision_from_map(text):
    entities = blocks(text)
    if not entities or properties(entities[0]).get("classname") != "worldspawn":
        raise ValueError("First entity must be worldspawn")
    for entity in entities[1:]:
        p = properties(entity)
        if p.get("classname") == "misc_prefab":
            raise ValueError(
                "Convert prefab entities to ordinary entities/brushes in Radiant before building"
            )
        if blocks(entity[1:-1]):
            raise ValueError(f'Brush entity {p.get("classname")} is unsupported; use world brushes')
    result = []
    for index, brush in enumerate(blocks(entities[0][1:-1])):
        if blocks(brush[1:-1]):
            raise ValueError(f"Brush {index}: patches/terrain are not supported by box collision")
        planes = re.findall(r"\(\s*([^()]+)\)\s*\(\s*([^()]+)\)\s*\(\s*([^()]+)\)\s*(\S+)", brush)
        if len(planes) != 6:
            raise ValueError(f"Brush {index}: expected six axial planes")
        lo, hi = [-math.inf] * 3, [math.inf] * 3
        textures = set()
        for a, b, c, texture in planes:
            a, b, c = map(vector, (a, b, c))
            u = [b[i] - a[i] for i in range(3)]
            v = [c[i] - a[i] for i in range(3)]
            n = [v[1] * u[2] - v[2] * u[1], v[2] * u[0] - v[0] * u[2], v[0] * u[1] - v[1] * u[0]]
            axes = [i for i in range(3) if abs(n[i]) > 1e-6]
            if len(axes) != 1:
                raise ValueError(
                    f"Brush {index}: non-axial collision; keep solid brushes square to the grid"
                )
            axis = axes[0]
            if n[axis] > 0:
                hi[axis] = min(hi[axis], a[axis])
            else:
                lo[axis] = max(lo[axis], a[axis])
            textures.add(texture)
        if textures == {"lightgrid_volume"}:
            continue
        if any(not -100000 <= lo[i] < hi[i] <= 100000 for i in range(3)):
            raise ValueError(f"Brush {index}: invalid bounds")
        # Only these tool materials have defined semantics in this solid-box path.
        unsupported = textures & {
            "portal",
            "occluder",
            "hint",
            "skip",
            "trigger",
            "clip_nosight",
            "clip_player",
            "clip_missile",
            "clip_vehicle",
            "nonsolid",
        }
        if unsupported:
            raise ValueError(
                f"Brush {index}: unsupported tool material {sorted(unsupported)}; use clip for solid boxes"
            )
        result.append({"mins": lo, "maxs": hi, "textures": sorted(textures)})
    if not 1 <= len(result) <= 4096:
        raise ValueError("Expected 1..4096 solid world brushes")
    return result


def encode_collision(brushes):
    data = (
        b"MWCOLL01"
        + struct.pack("<I", len(brushes))
        + b"".join(struct.pack("<6f", *(b["mins"] + b["maxs"])) for b in brushes)
    )
    validate_collision(data)
    return data


def validate_collision(data):
    if len(data) < 12 or data[:8] != b"MWCOLL01":
        raise ValueError("Bad collision.bin header")
    count = struct.unpack_from("<I", data, 8)[0]
    if not 1 <= count <= 4096 or len(data) != 12 + 24 * count:
        raise ValueError("Bad collision.bin length/count")
    for v in struct.iter_unpack("<6f", data[12:]):
        if any(not math.isfinite(x) for x in v) or any(
            not -100000 <= v[k] < v[k + 3] <= 100000 for k in range(3)
        ):
            raise ValueError("Invalid collision.bin bounds")
    return count
