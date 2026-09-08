"""Prepare validated Replay mesh, entities, materials and collision sidecars."""

import hashlib
import json
import math
import subprocess
import sys

from PIL import Image, ImageDraw

from .assets import Assets
from .core import cross, dot, encode_collision, sub, unit, vec, write_json


def packed_normal(n):
    n = unit(n)
    t = unit(cross([0, 0, 1] if abs(n[2]) < 0.9 else [0, 1, 0], n))
    b = cross(n, t)
    m = [[t[i], b[i], n[i]] for i in range(3)]
    trace = sum(m[i][i] for i in range(3))
    if trace > 0:
        s = math.sqrt(trace + 1) * 2
        q = [
            (m[2][1] - m[1][2]) / s,
            (m[0][2] - m[2][0]) / s,
            (m[1][0] - m[0][1]) / s,
            s / 4,
        ]
    else:
        i = max(range(3), key=lambda i: m[i][i])
        j = (i + 1) % 3
        k = (i + 2) % 3
        s = math.sqrt(1 + m[i][i] - m[j][j] - m[k][k]) * 2
        q = [0.0] * 4
        q[i] = s / 4
        q[j] = (m[j][i] + m[i][j]) / s
        q[k] = (m[k][i] + m[i][k]) / s
        q[3] = (m[k][j] - m[j][k]) / s
    largest = max(range(4), key=lambda i: (abs(q[i]), i))
    scale = math.sqrt(2) * (1 if q[largest] >= 0 else -1)
    values = [q[i] * scale for i in range(4) if i != largest]
    ints = [
        max(0, min((1 << bits) - 1, int((v + 1) * ((1 << bits) - 1) / 2)))
        for v, bits in zip(values, (10, 10, 9))
    ]
    return ints[0] | ints[1] << 10 | ints[2] << 20 | largest << 30


def spawns(scene, manual):
    classes = {
        "info_player_start",
        "info_player_deathmatch",
        "info_player_coop",
        "info_player_teamspawn",
        "info_player_terrorist",
        "info_player_counterterrorist",
        "mp_tdm_spawn",
        "mp_tdm_spawn_allies_start",
        "mp_tdm_spawn_axis_start",
        "mp_dm_spawn",
    }
    selected = []
    for e in scene.entities:
        if e.get("classname") in classes and "origin" in e:
            p = vec(e["origin"])
            angles = vec(e.get("angles", "0 " + e.get("angle", "0") + " 0"))
            selected.append((p, angles))
    selected += [(vec(p), [0, 0, 0]) for p in manual]
    if not selected:
        raise ValueError(
            "No compatible authored player spawns; supply --spawn X Y Z in output coordinates"
        )
    if any(abs(x) > 100000 for p, _ in selected for x in p):
        raise ValueError("Spawn outside Replay coordinate limits")
    fmt = lambda p: " ".join(format(v, ".9g") for v in p)
    result = ['{ 212 "worldspawn" }']

    def ent(cls, p, a):
        return '{ 212 "' + cls + '" 709 "' + fmt(p) + '" 80 "' + fmt(a) + '" }'

    for p, a in selected:
        result.append(ent("mp_tdm_spawn", p, a))
    result.append(ent("mp_tdm_spawn_allies_start", *selected[0]))
    result.append(ent("mp_tdm_spawn_axis_start", *selected[-1]))
    result.append(ent("info_player_start", *selected[0]))
    result.append('{ 212 "script_model" 709 "0 0 16" 80 "0 0 0" }')
    if len(selected) == 1:
        scene.warn(
            "Only one spawn location is available; team starts share it. Add separated spawns before multiplayer testing."
        )
    return "\n".join(result) + "\n", len(selected)


def split_surfaces(surfaces, tiles, textured):
    out = []
    for surface in surfaces:
        target = None
        remap = {}
        for offset in range(0, len(surface["indices"]), 3):
            triangle = surface["indices"][offset : offset + 3]
            ps = [surface["vertices"][i]["position"] for i in triangle]
            if (
                dot(
                    cross(sub(ps[1], ps[0]), sub(ps[2], ps[0])),
                    cross(sub(ps[1], ps[0]), sub(ps[2], ps[0])),
                )
                < 1e-12
            ):
                raise ValueError("Degenerate render triangle; repair source geometry")
            if (
                target is None
                or len(target["vertices"]) + 3 > 60000
                or len(target["indices"]) >= 65535 * 3
            ):
                target = {"vertices": [], "indices": []}
                out.append(target)
                remap = {}
            for index in (
                triangle[0],
                triangle[2],
                triangle[1],
            ):  # canonical CCW -> Replay CW
                if index not in remap:
                    v = surface["vertices"][index]
                    remap[index] = len(target["vertices"])
                    vertex = {
                        "position": v["position"],
                        "uv": v["uv"],
                        "normal": packed_normal(v["normal"]),
                    }
                    if textured:
                        vertex["lightmapUV"] = [
                            tiles[surface["material"]] + 0.375,
                            0.375,
                        ]
                    target["vertices"].append(vertex)
                target["indices"].append(remap[index])
    # Coalesce small source surfaces now that material choice is encoded per vertex.
    merged = []
    for s in out:
        if (
            merged
            and len(merged[-1]["vertices"]) + len(s["vertices"]) <= 60000
            and len(merged[-1]["indices"]) + len(s["indices"]) <= 65535 * 3
        ):
            previous = merged[-1]
            start = len(previous["vertices"])
            previous["vertices"].extend(s["vertices"])
            previous["indices"].extend(start + i for i in s["indices"])
        else:
            merged.append(s)
    if len(merged) > 4096:
        raise ValueError("Mesh exceeds Replay surface limit after partitioning")
    return merged


def prepare(scene, out, mapid, args):
    folder = out / "dump/maps/mp"
    folder.mkdir(parents=True)
    stem = mapid + ".d3dbsp"
    numeric, count = spawns(scene, args.spawn)
    (out / "dump" / (mapid + "_iw8_ents.txt")).write_text(numeric, encoding="ascii")
    (folder / (stem + ".ents")).write_text('{ "classname" "worldspawn" }\n', encoding="ascii")
    names = sorted({s["material"] for s in scene.surfaces})
    tiles = {name: i for i, name in enumerate(names)}
    tools = args.replay_tools
    textured = not args.graybox
    if textured and (tools is None or not (tools / "compile_graybox_shader.py").is_file()):
        raise ValueError(
            "Textured Replay export needs --replay-tools <mw120rproxy/tools>; use --graybox for a standalone package with a stock material"
        )
    resolved = []
    missing = []
    if textured:
        templates = tools.resolve().parents[1] / "custom_map_sources/mp_test"
        for relative in (
            "dump/maps/mp/mp_test.d3dbsp.material.json",
            "shaders/replay_static_world_techset.json",
        ):
            if not (templates / relative).is_file():
                raise ValueError(
                    "Missing local Replay shader templates. See docs/MAP_BUILDING.md#local-replay-shader-templates; "
                    "use --graybox to convert without source textures."
                )
        if len(names) > 1024:
            raise ValueError("More than 1024 atlas materials; split or consolidate the source map")
        columns = 1
        while columns * columns < len(names):
            columns *= 2
        size = 4096 // columns
        atlas = Image.new("RGBA", (4096, 4096), (64, 64, 64, 255))
        assets = Assets(args.asset_root, scene)
        for name, i in tiles.items():
            material = scene.materials.get(name, {})
            try:
                im = assets.image(material)
            except (ValueError, OSError) as error:
                if args.strict_textures:
                    raise
                scene.warn(f"Texture {name}: {error}")
                im = None
            if im is None:
                if material.get("texture") or material.get("texture_path"):
                    missing.append(name)
                    if args.strict_textures:
                        raise ValueError(f"Missing texture for {name}")
                color = material.get("color", [0.5, 0.5, 0.5])
                if any(not math.isfinite(c) or not 0 <= c <= 1 for c in color):
                    raise ValueError("Invalid material base color")
                im = Image.new("RGBA", (64, 64), tuple(round(c * 255) for c in color) + (255,))
                if name in missing:
                    draw = ImageDraw.Draw(im)
                    for y in range(0, 64, 16):
                        for x in range(0, 64, 16):
                            if (x + y) // 16 % 2:
                                draw.rectangle((x, y, x + 15, y + 15), fill=(150, 65, 150, 255))
            else:
                resolved.append(name)
                if "color" in material:
                    factors = material["color"]
                    if len(factors) != 3 or any(
                        not math.isfinite(c) or not 0 <= c <= 1 for c in factors
                    ):
                        raise ValueError("Invalid material color factor")
                    channels = list(im.split())
                    for channel, factor in enumerate(factors):
                        lut = []
                        for byte in range(256):
                            srgb = byte / 255
                            linear = (
                                srgb / 12.92 if srgb <= 0.04045 else ((srgb + 0.055) / 1.055) ** 2.4
                            )
                            linear *= factor
                            srgb = (
                                linear * 12.92
                                if linear <= 0.0031308
                                else 1.055 * linear ** (1 / 2.4) - 0.055
                            )
                            lut.append(round(srgb * 255))
                        channels[channel] = channels[channel].point(lut)
                    im = Image.merge("RGBA", channels)
                if im.getextrema()[3][0] < 255:
                    scene.warn(
                        "Some source color textures contain alpha; this generic import uses opaque geometry."
                    )
            if material.get("uv_texels"):
                for s in scene.surfaces:
                    if s["material"] == name:
                        for v in s["vertices"]:
                            v["uv"] = [v["uv"][0] / im.width, v["uv"][1] / im.height]
                if name in missing:
                    scene.warn(
                        "Missing Quake II texture dimensions use the visible checkerboard dimensions for preview UVs."
                    )
            atlas.paste(
                im.resize((size, size), Image.Resampling.LANCZOS),
                ((i % columns) * size, (i // columns) * size),
            )
        if missing:
            scene.warn(
                f"{len(missing)} material textures are missing; checkerboard substitutes are listed in the report. Use --asset-root and --strict-textures to require all source images."
            )
        repo = tools.resolve().parents[1]
        base = repo / "custom_map_sources/mp_test"
        material = json.loads((base / "dump/maps/mp/mp_test.d3dbsp.material.json").read_text())
        levels = max(1, int(math.log2(size)) - 1)
        chain = [atlas.tobytes()]
        mip = atlas
        for _ in range(1, levels):
            mip = mip.resize((mip.width // 2, mip.height // 2), Image.Resampling.BOX)
            chain.append(mip.tobytes())
        pixels = b"".join(chain)
        image_name = "mw120r/" + mapid + "_" + hashlib.sha256(pixels).hexdigest()[:16]
        material["textures"][0]["image"] = image_name
        material["techsetDefinition"] = stem + ".techset.json"
        material["imageDefinitions"] = [
            {
                "name": image_name,
                "width": 4096,
                "height": 4096,
                "rgba8": mapid + "_atlas.rgba",
                "mipCount": levels,
            }
        ]
        write_json(folder / (stem + ".material.json"), material)
        (folder / (mapid + "_atlas.rgba")).write_bytes(pixels)
        atlas.resize((1024, 1024)).save(out / "atlas.png")
        shader = (
            (tools / "map_surface.hlsl")
            .read_text()
            .replace("ATLAS_COLUMNS", str(columns))
            .replace("SUN_DIRECTION", "float3(.3,.4,.8660254)")
            .replace("SUN_COLOR", "float3(1,1,1)")
        )
        (out / "map.hlsl").write_text(shader)
        with (out / "shader.log").open("w") as log:
            subprocess.run(
                [
                    sys.executable,
                    str(tools / "compile_graybox_shader.py"),
                    "--source",
                    str(out / "map.hlsl"),
                    "--target-root",
                    str(out),
                    "--map",
                    mapid,
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=90,
            )
    else:
        scene.warn(
            "Graybox export uses the stock $default material; original textures are not in this package."
        )
    mesh = {
        "schema": 1,
        "material": "w/mw120r_" + mapid if textured else "$default",
        "surfaces": split_surfaces(scene.surfaces, tiles, textured),
    }
    if textured:
        mesh["materialDefinition"] = stem + ".material.json"
    write_json(folder / (stem + ".render.json"), mesh)
    (out / "collision.bin").write_bytes(encode_collision(scene.hulls))
    # Derive the native broadphase from all geometry and collision, not a fallback box.
    points = [v["position"] for s in scene.surfaces for v in s["vertices"]]
    points.extend(p for h in scene.hulls for p in h)
    bounds = {
        "schema": 1,
        "min": [min(p[k] for p in points) - 64 for k in range(3)],
        "max": [max(p[k] for p in points) + 64 for k in range(3)],
    }
    write_json(folder / (stem + ".bounds.json"), bounds)
    scene.stats.update(
        replay_surfaces=len(mesh["surfaces"]),
        spawn_locations=count,
        textures_resolved=len(resolved),
        textures_missing=len(missing),
        materials=len(names),
    )
    return {"resolved": resolved, "missing": missing, "textured": textured}


def preview(scene, path):
    """Offline geometry overview, explicitly not a game render."""
    triangles = []
    for surface in scene.surfaces:
        h = hashlib.sha256(surface["material"].encode()).digest()
        color = tuple(80 + x // 2 for x in h[:3])
        for i in range(0, len(surface["indices"]), 3):
            ps = [surface["vertices"][j]["position"] for j in surface["indices"][i : i + 3]]
            projected = [
                (
                    p[0] - 0.65 * p[1],
                    -0.30 * p[0] - 0.46 * p[1] - p[2],
                    p[0] + p[1] - p[2],
                )
                for p in ps
            ]
            normal = cross(sub(ps[1], ps[0]), sub(ps[2], ps[0]))
            shade = 0.45 + 0.55 * abs(dot(unit(normal), unit([0.3, 0.4, 1.0])))
            triangles.append(
                (
                    sum(p[2] for p in projected),
                    projected,
                    tuple(int(c * shade) for c in color),
                )
            )
    coords = [p for _, tri, _ in triangles for p in tri]
    mn = [min(p[k] for p in coords) for k in range(2)]
    mx = [max(p[k] for p in coords) for k in range(2)]
    scale = min(920 / max(1, mx[0] - mn[0]), 460 / max(1, mx[1] - mn[1]))
    im = Image.new("RGB", (1024, 576), (18, 20, 29))
    draw = ImageDraw.Draw(im)
    for _, tri, color in sorted(triangles, key=lambda t: t[0], reverse=True):
        points = [(52 + (p[0] - mn[0]) * scale, 60 + (p[1] - mn[1]) * scale) for p in tri]
        draw.polygon(points, fill=color)
    draw.text(
        (24, 18),
        scene.engine.upper() + " -> IW8 | offline geometry preview",
        fill=(230, 230, 245),
    )
    draw.text(
        (24, 550),
        f"{scene.stats['triangles']:,} triangles | {len(scene.hulls):,} collision hulls | no game test",
        fill=(180, 180, 200),
    )
    im.save(path)
