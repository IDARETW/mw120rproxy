"""Decode IW3's compiled lightmaps using the shipped lm_sun_r0c0 shader math."""

import math
from PIL import Image


def decode(root, pair):
    primary, secondary = pair
    if primary["format"] != 50 or secondary["format"] != 21 or secondary["height"] % 2:
        raise ValueError("Unsupported CoD4 lightmap format")
    w, h = secondary["width"], secondary["height"] // 2
    data = (root / secondary["file"]).read_bytes()
    if len(data) != w * h * 8:
        raise ValueError("Invalid secondary lightmap length")
    src = Image.frombytes("RGBA", (w, h * 2), data, "raw", "BGRA")
    top = src.crop((0, 0, w, h))
    bottom = src.crop((0, h, w, h * 2))
    colors = []
    for a, b in zip(top.getdata(), bottom.getdata()):
        nx = a[3] / 255 * 4.08 - 2.08
        ny = b[3] / 255 * 4.06451607 - 2.06451607
        weight = 1 / math.sqrt(1 + nx * nx + ny * ny)
        colors.append(tuple(round(min(255, (a[k] + b[k] * weight) * 0.5)) for k in range(3)))
    rgb = Image.new("RGB", (w, h))
    rgb.putdata(colors)
    raw = (root / primary["file"]).read_bytes()
    if len(raw) != primary["width"] * primary["height"]:
        raise ValueError("Invalid primary lightmap length")
    shadow = Image.frombytes("L", (primary["width"], primary["height"]), raw).resize(
        (w, h), Image.Resampling.BILINEAR
    )
    result = rgb.convert("RGBA")
    result.putalpha(shadow)
    return result


def pack(atlas, root, world, used_rows, cell):
    rectangles = []
    x = 0
    y = used_rows * cell
    rowheight = 0
    for pair in world.get("lightmaps", []):
        im = decode(root, pair)
        if x + im.width > atlas.width:
            x = 0
            y += rowheight
            rowheight = 0
        if y + im.height > atlas.height:
            raise ValueError(
                "Color and lightmap atlas exceeds 4096; increase the atlas packing budget"
            )
        atlas.paste(im, (x, y))
        rectangles.append((x, y, im.width, im.height))
        x += im.width
        rowheight = max(rowheight, im.height)
    return rectangles


def pack_coefficients(atlas, normals, response, root, world, used_rows, cell):
    """Keep both IW3 coefficients and their dominant direction for normal mapping."""
    rectangles = []
    x, y, rowheight = 0, used_rows * cell, 0
    for primary, secondary in world.get("lightmaps", []):
        if primary["format"] != 50 or secondary["format"] != 21 or secondary["height"] % 2:
            raise ValueError("Unsupported CoD4 lightmap format")
        w, h = secondary["width"], secondary["height"] // 2
        data = (root / secondary["file"]).read_bytes()
        if len(data) != w * h * 8:
            raise ValueError("Invalid secondary lightmap length")
        src = Image.frombytes("RGBA", (w, h * 2), data, "raw", "BGRA")
        first, second = src.crop((0, 0, w, h)), src.crop((0, h, w, 2 * h))
        direction = Image.merge(
            "RGBA",
            (
                first.getchannel("A"),
                second.getchannel("A"),
                first.getchannel("R"),
                first.getchannel("G"),
            ),
        )
        second.putalpha(first.getchannel("B"))
        raw = (root / primary["file"]).read_bytes()
        if len(raw) != primary["width"] * primary["height"]:
            raise ValueError("Invalid primary lightmap length")
        first.putalpha(
            Image.frombytes("L", (primary["width"], primary["height"]), raw).resize(
                (w, h), Image.Resampling.BILINEAR
            )
        )
        if x + w > atlas.width:
            x, y, rowheight = 0, y + rowheight, 0
        if y + h > atlas.height:
            raise ValueError("Material and lighting atlases exceed packing budget")
        for target, image in ((atlas, first), (normals, direction), (response, second)):
            target.paste(image, (x, y))
        rectangles.append((x, y, w, h))
        x += w
        rowheight = max(rowheight, h)
    return rectangles


def coordinates(rect, uv, size=4096):
    x, y, w, h = rect
    return [
        # Source UVs already address texel centers. Adding another half texel
        # and scaling by dimension-1 shifts every interior sample.
        (x + min(w - 0.5, max(0.5, uv[0] * w))) / size,
        (y + min(h - 0.5, max(0.5, uv[1] * h))) / size,
    ]


def sun(entity):
    angles = list(map(float, entity.get("sundirection", "-45 45 0").split()))
    pitch, yaw = map(math.radians, angles[:2])
    direction = [math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), -math.sin(pitch)]
    color = list(map(float, entity.get("suncolor", "1 1 1").split()))
    intensity = float(entity.get("sunlight", "1"))
    return direction, [c * intensity for c in color]


def sun_settings(direction, color):
    """Preserve source sun energy and chromaticity without the old brightness clamp."""
    if len(direction) != 3 or len(color) != 3:
        raise ValueError("Sun direction and color require three components")
    if any(not math.isfinite(v) for v in (*direction, *color)) or any(c < 0 for c in color):
        raise ValueError("Invalid source sun values")
    length = math.sqrt(sum(v * v for v in direction))
    if length < 1e-8:
        raise ValueError("Sun direction must be nonzero")
    peak = max(color)
    return {
        "schema": 1,
        "intensity": peak,
        "color": [c / peak if peak else 0.0 for c in color],
        "direction": [v / length for v in direction],
        "up": [0.0, 0.0, 0.0],
    }
