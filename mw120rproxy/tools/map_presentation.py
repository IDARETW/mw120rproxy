"""Resident mip chains, authored sun settings and local map artwork."""

import json, math, struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps
from source_atlases import resize_color, resize_data
from map_lighting import sun_settings

ROOT = Path(__file__).resolve().parents[2]


def sky_ambient(faces):
    """Solid-angle average in linear light; suppress strong artistic color casts."""
    if len(faces) != 6:
        raise ValueError("Ambient lighting requires six skybox faces")
    total = 0.0
    rgb = [0.0, 0.0, 0.0]
    for face in faces:
        pixels = face.convert("RGB").resize((32, 32), Image.Resampling.NEAREST)
        for y in range(32):
            for x in range(32):
                u = (x + 0.5) / 16 - 1
                v = (y + 0.5) / 16 - 1
                weight = (1 + u * u + v * v) ** -1.5
                total += weight
                for k, c in enumerate(pixels.getpixel((x, y))):
                    c /= 255
                    rgb[k] += weight * (c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    rgb = [v / total for v in rgb]
    luma = sum(a * b for a, b in zip(rgb, (0.2126, 0.7152, 0.0722)))
    # Native HDR irradiance is not a display RGB value. The old sub-one cap
    # left dark PBR surfaces underlit, especially without indoor light grids.
    # Supply two more stops of indirect light; retain sky tint and authored sun.
    strength = 7.0 * (0.12 + 0.32 * math.sqrt(luma))
    fill = [strength * (0.8 + 0.2 * min(2.0, c / max(luma, 0.001))) for c in rgb]
    fill = [min(4.0, max(0.32, c)) for c in fill]
    return {
        "linear_sky_average": rgb,
        "diffuse_fill": fill,
        "method": "linear cubemap solid-angle average, softened color",
    }


def ambient_bytes(fill):
    if len(fill) != 3 or any(not math.isfinite(c) or not 0.01 <= c <= 4 for c in fill):
        raise ValueError("Invalid ambient fill")
    # Exact Replay SH packing: nine half coefficients per color, then visibility.
    # Native isotropic shader reads DC at byte offsets 0,18,36. Lambert convolution
    # gives sqrt(pi)/2 = .886226925; every directional coefficient remains zero.
    probe = bytearray(64)
    for offset, c in zip((0, 18, 36), fill):
        struct.pack_into("<e", probe, offset, c / 0.886226925)
    struct.pack_into("<ee", probe, 54, 1.0, 1.0)
    return b"MWRAMB01" + probe


def mipmaps(folder, mapid, columns):
    folder = Path(folder)
    stem = mapid + ".d3dbsp"
    path = folder / (stem + ".material.json")
    material = json.loads(path.read_text())
    levels = int(math.log2(4096 // columns)) - 1  # stop while each tile still occupies 4x4 pixels
    for definition in material["imageDefinitions"]:
        file = folder / definition["rgba8"]
        w, h = definition["width"], definition["height"]
        im = Image.frombytes("RGBA", (w, h), file.read_bytes()[: w * h * 4])
        chain = [im.tobytes()]
        for level in range(1, levels):
            size = (max(1, im.width // 2), max(1, im.height // 2))
            im = (
                resize_data(im, size, Image.Resampling.BOX)
                if definition.get("format") == 6
                else resize_color(im, size, Image.Resampling.BOX)
            )
            chain.append(im.tobytes())
        file.write_bytes(b"".join(chain))
        definition["mipCount"] = levels
    path.write_text(json.dumps(material, indent=2) + "\n")


def prepare(folder, mapid, columns, direction, color, sky_faces=None):
    folder = Path(folder)
    stem = mapid + ".d3dbsp"
    mipmaps(folder, mapid, columns)
    lighting = sun_settings(direction, color)
    (folder / (stem + ".lighting.json")).write_text(json.dumps(lighting, indent=2) + "\n")


def preview(package, mapid, source=None):
    from prepare_textured_mp_test import decode_iwi

    package = Path(package)
    source = Path(source) if source else ROOT / f"custom_map_sources/{mapid}/extracted"
    candidate = source / f"images/loadscreen_{mapid}.iwi"
    canvas = Image.new("RGBA", (1024, 576), (20, 27, 35, 255))
    if candidate.exists():
        canvas = (
            decode_iwi(candidate.read_bytes())[0]
            .convert("RGBA")
            .resize(canvas.size, Image.Resampling.LANCZOS)
        )
        provenance = str(candidate)
    else:
        # Orthographic rendering of the compiled map.
        mesh = json.loads((package.parent / f"dump/maps/mp/{mapid}.d3dbsp.render.json").read_text())
        surfaces = [
            s
            for s in mesh["surfaces"]
            if int(s["vertices"][0].get("lightmapUV", [0, 0])[1]) % 4 != 1
        ]
        points = [v["position"] for s in surfaces for v in s["vertices"]]
        lo = [min(p[k] for p in points) for k in (0, 1)]
        hi = [max(p[k] for p in points) for k in (0, 1)]
        scale = min(900 / max(1, hi[0] - lo[0]), 420 / max(1, hi[1] - lo[1]))
        draw = ImageDraw.Draw(canvas)
        triangles = []
        for s in surfaces:
            vv = s["vertices"]
            tile = int(vv[0].get("lightmapUV", [0, 0])[0])
            color = (80 + tile * 37 % 90, 100 + tile * 13 % 85, 100 + tile * 29 % 75, 255)
            for i in range(0, len(s["indices"]), 3):
                p = [vv[j]["position"] for j in s["indices"][i : i + 3]]
                triangles.append(
                    (
                        sum(v[2] for v in p),
                        [
                            (
                                512 + (v[0] - (lo[0] + hi[0]) / 2) * scale,
                                320 - (v[1] - (lo[1] + hi[1]) / 2) * scale,
                            )
                            for v in p
                        ],
                        color,
                    )
                )
        for _, xy, color in sorted(triangles, key=lambda t: t[0]):
            draw.polygon(xy, fill=color)
        font = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 32)
        draw.text((48, 28), "MP_TEST  /  RADIANT", font=font, fill="white")
        draw.text(
            (48, 72),
            "Custom arena - compiled map overview",
            fill=(173, 192, 211),
            font=ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 18),
        )
        provenance = "Orthographic render of compiled " + mapid + " geometry"
    (package / "preview.rgba").write_bytes(
        struct.pack("<4I", 0x4952574D, 1, 1024, 576) + canvas.tobytes()
    )
    canvas.save(package.parent / "preview.png")
    manifest = json.loads((package / "manifest.json").read_text())
    manifest["preview"] = "rgba8-v1"
    (package / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (package.parent / "preview_source.json").write_text(
        json.dumps({"source": provenance}, indent=2) + "\n"
    )
