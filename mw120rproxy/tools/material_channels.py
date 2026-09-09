"""Retain the material channels and constants used by the IW3 lit shaders."""

import json
import math
from pathlib import Path
import struct

DEFAULT_ENVIRONMENT = [0.8, 4.0, 2.5, 0.625]


def describe(root, name, raw=None):
    if not name or any(s in name for s in ("..", ":", "\\")) or name.startswith("/"):
        raise ValueError("Invalid material name")
    path = Path(root) / "materials" / (name + ".json")
    textures, constants = {}, {}
    if path.is_file():
        material = json.loads(path.read_text())
        textures = {t["semantic"]: t["image"] for t in material.get("textures", [])}
        constants = {c["name"]: c["literal"] for c in material.get("constants", [])}
    elif raw is not None:
        path = Path(raw) / "materials" / name.removeprefix("mc/").removeprefix("wc/")
        data = path.read_bytes()
        if len(data) < 64:
            raise ValueError("Truncated raw material")

        def string(offset):
            end = data.find(b"\0", offset, offset + 512)
            if not 0 <= offset < len(data) or end < 0:
                raise ValueError("Invalid raw material string")
            return data[offset:end].decode("ascii")

        table = struct.unpack_from("<I", data, 56)[0]
        for index in range(data[48]):
            offset = table + index * 12
            if offset + 12 > len(data):
                raise ValueError("Truncated raw material texture table")
            key, _, image = struct.unpack_from("<III", data, offset)
            textures[string(key)] = string(image)
        table = struct.unpack_from("<I", data, 60)[0]
        for index in range(data[50]):
            offset = table + index * 20
            if offset + 20 > len(data):
                raise ValueError("Truncated raw material constant table")
            key, *values = struct.unpack_from("<I4f", data, offset)
            constants[string(key)] = values
    tint = constants.get("colorTint", [1.0] * 4)
    environment = constants.get("envMapParms", DEFAULT_ENVIRONMENT)
    for values in (tint, environment):
        if len(values) != 4 or any(not math.isfinite(v) or not 0 <= v <= 64 for v in values):
            raise ValueError("Invalid source material constant")
    return {
        "normal_image": textures.get("normalMap"),
        "specular_image": textures.get("specularMap"),
        "color_tint": tint,
        "environment": environment,
    }


def tile_key(material):
    """Different normal/response maps cannot alias just because color images match."""
    return (
        material["image"],
        material.get("normal_image") or "",
        material.get("specular_image") or "",
        tuple(material.get("color_tint", [1.0] * 4)),
    )


def source_normal(alpha, green):
    """IW3 stores tangent slopes, not conventional DXT5nm unit-vector components."""
    x, y = alpha * 4.08 - 2.08, green * 4.06451607 - 2.06451607
    length = math.sqrt(1 + x * x + y * y)
    return [x / length, y / length, 1 / length]


def directional_light(first, second, direction_alpha, tangent_normal):
    """CPU reference for the shipped lm_sun_r0c0n0 diffuse coefficient equation."""
    direction = source_normal(*direction_alpha)
    response = max(0, min(1, sum(a * b for a, b in zip(direction, tangent_normal))))
    return [a * tangent_normal[2] + b * response for a, b in zip(first, second)]
