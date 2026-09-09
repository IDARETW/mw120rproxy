"""Pack source color, tangent-normal, and specular data without channel aliasing."""

from PIL import Image, ImageMath
from material_channels import tile_key


def resize_data(image, size, resample=Image.Resampling.BILINEAR):
    """Filter independent RGBA channels without treating A as transparency."""
    channels = (c.resize(size, resample) for c in image.convert("RGBA").split())
    return Image.merge("RGBA", tuple(channels))


def resize_color(image, size, resample=Image.Resampling.BILINEAR):
    """Filter transparent color in float precision, then store straight RGBA8."""
    image = image.convert("RGBA")
    if image.size == size:
        return image.copy()
    if image.getchannel("A").getextrema() == (255, 255):
        return image.resize(size, resample)
    # Pillow's RGBA resize premultiplies in eight bits. At small alpha values
    # that rounds brown/gray detail to saturated RGB when it is divided back.
    # Positive filter weights also keep cutout edges free of Lanczos ringing.
    if resample not in (Image.Resampling.NEAREST, Image.Resampling.BOX):
        resample = Image.Resampling.BILINEAR
    alpha = image.getchannel("A").convert("F")
    filtered_alpha = alpha.resize(size, resample)
    denominator = ImageMath.lambda_eval(
        lambda d: (d["a"] > 0) * d["a"] + (d["a"] <= 0), a=filtered_alpha
    )
    linear = [
        c / 255 / 12.92 if c / 255 <= 0.04045 else ((c / 255 + 0.055) / 1.055) ** 2.4
        for c in range(256)
    ]
    channels = []
    for channel in image.split()[:3]:
        color = channel.point(linear, "F")
        weighted = ImageMath.lambda_eval(lambda d: d["c"] * d["a"], c=color, a=alpha)
        color = ImageMath.lambda_eval(
            lambda d: d["c"] / d["a"], c=weighted.resize(size, resample), a=denominator
        )
        encoded = ImageMath.lambda_eval(
            lambda d: 255
            * (
                (d["c"] <= 0.0031308) * d["c"] * 12.92
                + (d["c"] > 0.0031308) * (1.055 * d["c"] ** (1 / 2.4) - 0.055)
            )
            + 0.5,
            c=color,
        )
        channels.append(encoded.convert("L"))
    channels.append(ImageMath.lambda_eval(lambda d: d["a"] + 0.5, a=filtered_alpha).convert("L"))
    return Image.merge("RGBA", tuple(channels))


def tint_image(image, tint):
    if any(v > 1 for v in tint):
        raise ValueError("HDR colorTint requires a floating-point material atlas")
    image = image.convert("RGBA")
    if all(v == 1 for v in tint):
        return image
    tables = []
    for channel, strength in enumerate(tint):
        values = []
        for value in range(256):
            sample = value / 255
            if channel != 3:
                sample = sample / 12.92 if sample <= 0.04045 else ((sample + 0.055) / 1.055) ** 2.4
            sample *= strength
            if channel != 3:
                sample = (
                    sample * 12.92 if sample <= 0.0031308 else 1.055 * sample ** (1 / 2.4) - 0.055
                )
            values.append(round(sample * 255))
        tables.extend(values)
    return image.point(tables)


def pack(materials, surfaces, load_image):
    keys = sorted({tile_key(materials[s["material"]]) for s in surfaces})
    if len(keys) > 250:
        raise ValueError(f"{len(keys)} material combinations exceed the 250-tile atlas budget")
    columns = 4
    while columns * columns < len(keys) + 6:
        columns *= 2
    cell = 4096 // columns
    atlases = [Image.new("RGBA", (4096, 4096)) for _ in range(3)]
    for tile, key in enumerate(keys):
        color, normal, response, tint = key
        for channel, name in enumerate((color, normal, response)):
            if not name:
                continue
            faces = load_image(name)
            if len(faces) != 1:
                raise ValueError(f"{name}: expected 2D material image")
            image = tint_image(faces[0], tint) if channel == 0 else faces[0]
            # Data channels must not ring across sharp material boundaries.
            filter = Image.Resampling.LANCZOS if channel == 0 else Image.Resampling.BILINEAR
            resized = (
                resize_color(image, (cell, cell), filter)
                if channel == 0
                else resize_data(image, (cell, cell), filter)
            )
            atlases[channel].paste(
                resized,
                ((tile % columns) * cell, (tile // columns) * cell),
            )
    return atlases, keys, columns, cell


def refresh(folder, mapid, report, columns, load_image):
    """Rebuild material tiles while retaining the sky and authored lightmap texels."""
    import hashlib
    import json
    from pathlib import Path
    from map_presentation import mipmaps

    folder = Path(folder)
    keys = sorted({tile_key(m) for m in report["materials"].values() if "image" in m})
    if [key[0] for key in keys] != report["color_images"]:
        raise ValueError("Retained material tile order does not match source material keys")
    path = folder / (mapid + ".d3dbsp.material.json")
    material = json.loads(path.read_text())
    definitions = material["imageDefinitions"]
    if len(definitions) != 3 or [d.get("format") for d in definitions] != [7, 6, 6]:
        raise ValueError("Expected a color atlas and two linear source-data atlases")
    cell = 4096 // columns
    renamed = {}
    for channel, definition in enumerate(definitions):
        if (definition["width"], definition["height"]) != (4096, 4096):
            raise ValueError("Unexpected retained atlas dimensions")
        file = folder / definition["rgba8"]
        atlas = Image.frombytes("RGBA", (4096, 4096), file.read_bytes()[: 4096 * 4096 * 4])
        for tile, key in enumerate(keys):
            name = key[channel]
            if not name:
                continue
            faces = load_image(name)
            if len(faces) != 1:
                raise ValueError(f"{name}: expected 2D material image")
            image = (
                resize_color(tint_image(faces[0], key[3]), (cell, cell), Image.Resampling.LANCZOS)
                if channel == 0
                else resize_data(faces[0], (cell, cell))
            )
            atlas.paste(image, ((tile % columns) * cell, (tile // columns) * cell))
        file.write_bytes(atlas.tobytes())
    mipmaps(folder, mapid, columns)
    material = json.loads(path.read_text())
    for definition in material["imageDefinitions"]:
        digest = hashlib.sha256((folder / definition["rgba8"]).read_bytes()).hexdigest()[:16]
        name = definition["name"].rsplit("_", 1)[0] + "_" + digest
        renamed[definition["name"]] = name
        definition["name"] = name
    for texture in material["textures"]:
        texture["image"] = renamed.get(texture["image"], texture["image"])
    path.write_text(json.dumps(material, indent=2) + "\n")
    return {
        "version": 3,
        "material_tiles": len(keys),
        "data_filter": "independent-channels",
        "color_filter": "float-premultiplied-alpha",
    }
