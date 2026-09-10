"""Convert an offline OAT IW3 map dump into its own selectable Replay package."""

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import gc
import json
import math
from pathlib import Path
import re
import sys
from PIL import Image
from build_mp_test import REPO, TOOLS, BASE, REPLAY, COD4, run, write_json, numeric_entities
from imported_map_assets import (
    read_material,
    read_obj,
    place,
    packed_normal,
    brush_hull,
    safe_asset,
)
from prepare_textured_mp_test import decode_iwi, sky_surfaces
from cod4_assets import Images, rotation
from foliage_mesh import mask_rectangles, mask_geometry, cutout
from radiant_source import blocks, properties, vector
import radiant_collision
import ladder_data
import glass_panes
import map_lighting
import window_entities
import door_data
from foliage_material import classify
from vertex_attributes import color, coverage_flags, atlas_vertices
from material_channels import tile_key
import source_atlases


def merge_surfaces(surfaces):
    output = []
    active = {}
    for s in surfaces:
        if not s["indices"]:
            continue
        key = (s["material"], s.get("glassPane"), s.get("door"), s.get("doorFrame"))
        target = active.get(key)
        if len(s["vertices"]) > 60000 or len(s["indices"]) > 65535 * 3:
            raise ValueError("Source surface exceeds native mesh limits")
        if (
            target is None
            or len(target["vertices"]) + len(s["vertices"]) > 60000
            or len(target["indices"]) + len(s["indices"]) > 65535 * 3
        ):
            target = {"material": s["material"], "vertices": [], "indices": []}
            if "glassPane" in s:
                target["glassPane"] = s["glassPane"]
            for field in ("door", "doorFrame"):
                if field in s:
                    target[field] = s[field]
            active[key] = target
            output.append(target)
        first = len(target["vertices"])
        target["vertices"].extend(s["vertices"])
        target["indices"].extend(i + first for i in s["indices"])
    return output


def build(args):
    if not math.isfinite(args.sun_intensity_scale) or args.sun_intensity_scale <= 0:
        raise ValueError("Sun intensity scale must be finite and positive")
    mapid = args.map
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", mapid):
        raise ValueError("Invalid map id")
    source = args.dump.resolve()
    world = json.loads((source / f"maps/mp/{mapid}.d3dbsp.replay-world.json").read_text())
    collision = json.loads((source / f"maps/mp/{mapid}.d3dbsp.replay-collision.json").read_text())
    if world["name"] != f"maps/mp/{mapid}.d3dbsp" or collision["name"] != world["name"]:
        raise ValueError("Mismatched map intermediates")
    parent = (
        args.output_root.resolve() / mapid
        if args.output_root
        else REPO / f"custom_map_sources/{mapid}/builds"
    )
    out = parent / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    out.mkdir(parents=True)
    print(f"Build output: {out}", flush=True)
    entitytext = (source / f"maps/mp/{mapid}.d3dbsp.ents").read_text()
    entities = [properties(b) for b in blocks(entitytext)]
    spawn_entities = entities
    if args.spawn_mode == "deathrun-tdm":
        spawn_entities = list(entities)
        for e in entities:
            if e.get("classname") in ("mp_jumper_spawn", "mp_activator_spawn"):
                team = "allies" if e["classname"] == "mp_jumper_spawn" else "axis"
                spawn_entities.extend(
                    [
                        {**e, "classname": "mp_tdm_spawn"},
                        {**e, "classname": f"mp_tdm_spawn_{team}_start"},
                    ]
                )
        first = next((e for e in entities if e.get("classname") == "mp_jumper_spawn"), None)
        if first:
            spawn_entities.append({**first, "classname": "info_player_start"})
    numeric, spawns, omitted_entities = numeric_entities(spawn_entities)
    hidden, windows = window_entities.states(entities)

    def visible_entity(e):
        return (
            bool(e)
            and id(e) not in hidden
            and e.get("targetname") != "exploder"
            and not e.get("script_fxid")
            and e.get("script_gameobjectname", "tdm") == "tdm"
        )

    brush_entities = {
        int(e["model"][1:]): e for e in entities if re.fullmatch(r"\*\d+", e.get("model", ""))
    }
    script = source / f"maps/mp/{mapid}.gsc"
    doors = door_data.discover(entities, world, script.read_text() if script.exists() else "")
    door_models = {d["model"]: i for i, d in enumerate(doors)}
    door_entities = {id(brush_entities[m]): index for m, index in door_models.items()}
    surface_entities = {}
    for index, m in enumerate(world["brush_models"][1:], 1):
        for si in range(m["start"], m["start"] + m["count"]):
            surface_entities[si] = brush_entities.get(index)

    def transform(v, e):
        matrix = rotation(vector(e.get("angles", "0 0 0")))
        origin = vector(e.get("origin", "0 0 0"))
        return [sum(matrix[i][j] * v[j] for j in range(3)) + origin[i] for i in range(3)]

    materials = {}
    omitted = Counter()
    surfaces = []
    model_cache = {}
    model_repairs = {}

    def include(name):
        if name not in materials:
            materials[name] = read_material(source, name)
        if "skip" in materials[name]:
            omitted[name] += 1
            return False
        return True

    images = {}
    fallback = Images(COD4, COD4 / "raw")
    foliage = []
    masks = {}

    def image(name):
        if name not in images:
            p = safe_asset(source, "images", name, ".iwi")
            try:
                images[name] = decode_iwi(p.read_bytes()) if p.is_file() else fallback.get(name)
            except (ValueError, FileNotFoundError) as error:
                raise ValueError(f"Cannot convert source image {name!r}: {error}") from error
        return images[name]

    def alpha_convert(s):
        material = materials[s["material"]]
        classify(s["material"], material, image(material["image"])[0])
        if material.get("cutout"):
            foliage.append(
                {
                    "material": s["material"],
                    "method": "gpu_alpha_test",
                    "threshold": 128,
                    "input_triangles": len(s["indices"]) // 3,
                }
            )
        return [s]

    from source_tjunctions import stitch

    architecture = []
    for si, surface in enumerate(world["surfaces"]):
        if si in surface_entities or not include(surface["material"]):
            continue
        material = materials[surface["material"]]
        classify(surface["material"], material, image(material["image"])[0])
        if not material.get("cutout") and not material.get("blended"):
            architecture.append(surface)
    junction_report = stitch(architecture)
    print(
        "Source edge stitching: "
        + str({k: v for k, v in junction_report.items() if k != "changed_surfaces"}),
        flush=True,
    )

    for si, s in enumerate(world["surfaces"]):
        entity = surface_entities.get(si)
        if si in surface_entities and (
            not visible_entity(entity) or entity.get("classname") != "script_brushmodel"
        ):
            continue
        if not include(s["material"]):
            continue
        vertices = []
        for v in s["vertices"]:
            position = v["position"]
            normal = v["normal"]
            tangent = v["tangent"]
            if entity:
                position = transform(position, entity)
                matrix = rotation(vector(entity.get("angles", "0 0 0")))
                normal = [sum(matrix[i][j] * normal[j] for j in range(3)) for i in range(3)]
                tangent = [sum(matrix[i][j] * tangent[j] for j in range(3)) for i in range(3)]
            vertices.append(
                {
                    "position": position,
                    "uv": v["uv"],
                    "normal": packed_normal(normal, tangent, v["binormal_sign"]),
                    "baked_uv": v.get("lightmap_uv", [0, 0]),
                    "baked_index": s.get("lightmap", 255),
                    "color": color(v),
                }
            )
        surface = {**s, "vertices": vertices}
        if id(entity) in door_entities:
            surface["door"] = door_entities[id(entity)]
        if id(entity) in windows:
            surface["glassGroup"] = windows[id(entity)]
        surfaces.extend(alpha_convert(surface))
    instances = list(world["models"])
    for e in entities:
        if e.get("classname") != "script_model" or not e.get("model") or not visible_entity(e):
            continue
        matrix = rotation(vector(e.get("angles", "0 0 0")))
        instances.append(
            {
                "model": e["model"].removeprefix("xmodel/"),
                "origin": vector(e.get("origin", "0 0 0")),
                "axis": [list(v) for v in zip(*matrix)],
                "scale": float(e.get("modelscale", 1)),
                **({"glassGroup": windows[id(e)]} if id(e) in windows else {}),
            }
        )
    for instance_index, instance in enumerate(instances):
        if instance_index % 100 == 0:
            print(
                f"Baking prop {instance_index+1}/{len(instances)} ({len(model_cache)} unique models)",
                flush=True,
            )
        name = instance["model"]
        if name not in model_cache:
            model_cache[name] = []
            for s in read_obj(source, name):
                if include(s["material"]):
                    material = materials[s["material"]]
                    classify(s["material"], material, image(material["image"])[0])
                    if not material.get("cutout") and not material.get("blended"):
                        from model_internal_faces import remove_internal_faces

                        s, repair = remove_internal_faces(s)
                        if repair["changed_triangles"]:
                            model_repairs.setdefault(name, []).append(repair)
                    model_cache[name].extend(
                        alpha_convert(place(s, [0, 0, 0], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], 1))
                    )
        for s in model_cache[name]:
            surface = place(s, instance["origin"], instance["axis"], instance["scale"])
            if "glassGroup" in instance:
                surface["glassGroup"] = instance["glassGroup"]
            surfaces.append(surface)
    from decal_geometry import separate

    decal_report = separate(surfaces, materials)
    print("Decal separation: " + str(decal_report), flush=True)
    surfaces = door_data.poses(surfaces, doors)
    moving = [s for s in surfaces if "door" in s]
    surfaces, panes = glass_panes.prepare([s for s in surfaces if "door" not in s], materials)
    surfaces.extend(moving)
    del moving
    surfaces = merge_surfaces(surfaces)
    print(
        f'Geometry: {len(surfaces)} surfaces, {sum(len(s["indices"])//3 for s in surfaces)} triangles',
        flush=True,
    )
    atlases, keys, columns, cell = source_atlases.pack(materials, surfaces, image)
    atlas, normal_atlas, response_atlas = atlases
    names = [key[0] for key in keys]
    tiles = {key: i for i, key in enumerate(keys)}
    skyname = world["sky"]
    sky = image(skyname)
    if len(sky) != 6:
        raise ValueError(f"{skyname}: expected cubemap")
    for i, face in enumerate(sky, start=len(names)):
        atlas.paste(
            face.resize((cell, cell), Image.Resampling.LANCZOS),
            ((i % columns) * cell, (i // columns) * cell),
        )
    lightmaps = map_lighting.pack_coefficients(
        atlas,
        normal_atlas,
        response_atlas,
        source,
        world,
        math.ceil((len(names) + 6) / columns),
        cell,
    )
    has_cutout = any(m.get("cutout") for m in materials.values())
    has_glass = any(m.get("blended") for m in materials.values())
    for s in surfaces:
        mat = materials[s.pop("material")]
        tile = tiles[tile_key(mat)]
        s["materialParameters"] = mat["environment"]
        kind = 3 if mat.get("cutout") else (2 if mat.get("blended") else 0)
        s["materialIndex"] = 1 if kind == 3 else ((2 if has_cutout else 1) if kind == 2 else 0)
        s["vertices"] = atlas_vertices(
            s["vertices"],
            tile,
            kind
            + coverage_flags(mat)
            + (32 if mat.get("normal_image") else 0)
            + (64 if mat.get("specular_image") else 0),
            lightmaps,
        )
    for i, s in enumerate(sky_surfaces()):
        for v in s["vertices"]:
            v["lightmapUV"] = [len(names) + i + 0.25, 1.25]
        s["renderClass"] = "sky"
        s["materialIndex"] = 1 + int(has_cutout) + int(has_glass)
        surfaces.append(s)
    glass_index = 2 if has_cutout else 1
    surfaces.sort(
        key=lambda s: (
            bool(has_glass and s.get("materialIndex", 0) == glass_index),
            s.get("materialIndex", 0),
        )
    )
    (out / "glass.bin").write_bytes(glass_panes.encode(panes, surfaces))
    blended_surfaces = (
        sum(s.get("materialIndex", 0) == (2 if has_cutout else 1) for s in surfaces)
        if has_glass
        else 0
    )
    if len(surfaces) > 4096:
        raise ValueError("Too many render surfaces")
    brushes = []
    ladders = []
    skipped_contents = Counter()
    brush_owners = {}

    def leaf_brushes(root):
        if root == 0:
            return set()  # CoD4's explicit empty brush-leaf sentinel.
        todo = [root]
        seen = set()
        result = set()
        nodes = collision["leaf_brush_nodes"]
        while todo:
            n = todo.pop()
            if n in seen:
                continue
            if not 0 < n < len(nodes):
                raise ValueError("Invalid compiled leaf brush node")
            seen.add(n)
            node = nodes[n]
            if node["count"] > 0:
                result.update(node["brushes"])
            else:
                if node["count"] < 0:
                    todo.append(n + 1)
                todo.extend(n + offset for offset in node["children"] if offset)
        return result

    for i, m in enumerate(collision["submodels"][1:], 1):
        for b in leaf_brushes(m["leaf"]):
            if b in brush_owners:
                raise ValueError("Brush belongs to multiple submodels")
            brush_owners[b] = brush_entities.get(i)
    for i, b in enumerate(collision["brushes"]):
        if not radiant_collision.contents(b):
            skipped_contents[str(b["contents"])] += 1
            continue
        entity = brush_owners.get(i)
        if i in brush_owners and (
            not visible_entity(entity) or entity.get("classname") != "script_brushmodel"
        ):
            continue
        if id(entity) in door_entities:
            door_data.add_hull(doors[door_entities[id(entity)]], b, entity)
            continue
        h = brush_hull(b, i)
        if entity:
            h["vertices"] = [transform(v, entity) for v in h["vertices"]]
        brushes.append(h)
        if not entity:
            ladders.extend(ladder_data.faces(b))
    triangle_report = {}
    triangle_hulls = radiant_collision.add_compiled_triangles(brushes, collision, triangle_report)
    brushes, glass_collision_removed = glass_panes.remove_static_collision(brushes, panes)
    if doors:
        (out / "doors.bin").write_bytes(door_data.encode(doors, surfaces))
        write_json(out / "doors.json", doors)
    write_json(out / "source_collision.json", brushes)
    ladder_matches = ladder_data.align_models(ladders, instances, source)
    (out / "ladders.bin").write_bytes(ladder_data.encode(ladders))
    folder = out / "dump/maps/mp"
    folder.mkdir(parents=True)
    stem = mapid + ".d3dbsp"
    (folder / (stem + ".ents")).write_text(entitytext, encoding="ascii")
    (out / f"dump/{mapid}_iw8_ents.txt").write_text(numeric, encoding="ascii")
    with (folder / (stem + ".render.json")).open("w", encoding="utf-8") as mesh_output:
        json.dump(
            {
                "schema": 1,
                "material": f"w/mw120r_{mapid}",
                "materialDefinition": stem + ".material.json",
                "additionalMaterials": [
                    {
                        "schema": 1,
                        "material": f"w/mw120r_{mapid}_{kind}",
                        "materialDefinition": stem + "." + kind + ".material.json",
                    }
                    for kind, enabled in [
                        ("foliage", has_cutout),
                        ("glass", has_glass),
                        ("sky", True),
                    ]
                    if enabled
                ],
                "surfaces": surfaces,
            },
            mesh_output,
            separators=(",", ":"),
        )
    material = json.loads((BASE / "dump/maps/mp/mp_test.d3dbsp.material.json").read_text())
    material["techsetDefinition"] = stem + ".techset.json"
    material["imageDefinitions"] = []
    for channel, target in enumerate(atlases):
        pixels = target.tobytes()
        image_name = f"mw120r/{mapid}_{channel}_" + hashlib.sha256(pixels).hexdigest()[:16]
        filename = f"{mapid}_atlas_{channel}.rgba"
        material["textures"][channel]["image"] = image_name
        material["imageDefinitions"].append(
            {
                "name": image_name,
                "width": 4096,
                "height": 4096,
                "rgba8": filename,
                "format": 7 if channel == 0 else 6,
            }
        )
        (folder / filename).write_bytes(pixels)
    write_json(folder / (stem + ".material.json"), material)
    atlas.save(out / "atlas.png")
    direction, sun_color = map_lighting.sun(entities[0])
    from map_presentation import prepare

    prepare(folder, mapid, columns, direction, sun_color, sky)
    shader = "#define MAP_SOURCE_CHANNELS 1\n" + (
        (TOOLS / "map_surface_realtime.hlsl").read_text().replace("ATLAS_COLUMNS", str(columns))
    )
    if args.lighting_profile == "source":
        shader = "#define MAP_SOURCE_SUN_MASK 1\n" + shader
    (out / "map.hlsl").write_text(shader)
    run(
        [
            sys.executable,
            TOOLS / "compile_graybox_shader.py",
            "--source",
            out / "map.hlsl",
            "--target-root",
            out,
            "--map",
            mapid,
        ],
        REPO,
        out / "shader.log",
    )
    from glass_material import create

    for kind, enabled in [("foliage", has_cutout), ("glass", has_glass), ("sky", True)]:
        if enabled:
            create(folder, stem, mapid, kind)
    converter = REPO.parent / "iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe"
    package = out / "package"
    report = {
        "map": mapid,
        "title": args.title,
        "credit": args.credit,
        "source": str(source),
        "package": str(package),
        "surfaces": len(surfaces),
        "vertices": sum(len(s["vertices"]) for s in surfaces),
        "triangles": sum(len(s["indices"]) // 3 for s in surfaces),
        "models": len(world["models"]),
        "unique_models": len(model_cache),
        "model_internal_faces": model_repairs,
        "materials": materials,
        "decal_separation": decal_report,
        "source_edge_stitching": junction_report,
        "omitted_materials": dict(omitted),
        "spawn_mode": args.spawn_mode,
        "color_images": names,
        "source_channels": {
            "version": 1,
            "normal_materials": sum(bool(m.get("normal_image")) for m in materials.values()),
            "specular_materials": sum(bool(m.get("specular_image")) for m in materials.values()),
            "directional_lightmaps": True,
        },
        "atlas_cell": cell,
        "sky": skyname,
        "foliage": foliage,
        "spawns": spawns,
        "omitted_entities": omitted_entities,
        "collision_hulls": len(brushes),
        "triangle_hulls": triangle_hulls,
        "triangle_collision": triangle_report,
        "glass_collision_removed": glass_collision_removed,
        "hidden_window_states": len(hidden),
        "scripted_windows": len(windows),
        "doors": [{k: v for k, v in d.items() if k != "hulls"} for d in doors],
        "skipped_brush_contents": dict(skipped_contents),
        "static_collision_models": collision["static_models"],
        "submodel_count": collision["submodel_count"],
        "game_tested": False,
        "foliage_mask_size": args.foliage_mask_size,
        "ladder_faces": len(ladders),
        "blended_surfaces": blended_surfaces,
        "glass_panes": len(panes),
        "measured_ladder_faces": ladder_matches,
        "lightmaps": len(lightmaps),
        "sun_direction": direction,
        "sun_color": sun_color,
    }
    write_json(out / "import_report.json", report)
    from build_mp_test import bake_native_collision

    bake_native_collision(out, mapid, source, materials, names, surfaces)
    del (
        surfaces,
        world,
        model_cache,
        images,
        brushes,
        collision,
        atlases,
        atlas,
        normal_atlas,
        response_atlas,
        pixels,
        target,
    )
    gc.collect()
    from native_lightgrid import convert as convert_lightgrid

    convert_lightgrid(source, mapid, out / f"dump/maps/mp/{mapid}.d3dbsp.gpulightgrid.bin")
    run(
        [
            converter,
            "fromdump",
            out / "dump",
            mapid,
            "-o",
            package,
            "--stored",
            "--lighting-profile",
            args.lighting_profile,
            "--sun-intensity-scale",
            str(args.sun_intensity_scale),
        ],
        REPO,
        out / "convert.log",
    )
    from finish_imported_map import finish

    finish(out, mapid, args.title, args.credit, source)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, required=True)
    parser.add_argument("--map", required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--credit", default="")
    parser.add_argument("--output-root", type=Path)
    parser.add_argument(
        "--lighting-profile", choices=("source", "aniyah-incursion"), default="source"
    )
    parser.add_argument("--sun-intensity-scale", type=float, default=6.0)
    parser.add_argument(
        "--spawn-mode", choices=("native-tdm", "deathrun-tdm"), default="native-tdm"
    )
    parser.add_argument("--foliage-mask-size", type=int, choices=(16, 32, 64, 128), default=32)
    build(parser.parse_args())
