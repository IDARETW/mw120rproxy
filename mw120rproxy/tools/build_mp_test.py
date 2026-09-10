"""Compile an authored CoD4 Radiant map and bake static props into a Replay mp_test package.

Every run preserves its source, compiler log, intermediate assets and package in
a new directory. No game is launched. Use --deploy with Replay closed to install.
"""

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from PIL import Image
from cod4_assets import (
    Images,
    load_model,
    material_image,
    transformed,
    BlendedMaterial,
    ShadowOnlyMaterial,
)
from foliage_mesh import mask_rectangles, mask_geometry, cutout
from prepare_cod4_map import read_bsp, geometry
from prepare_textured_mp_test import sky_surfaces
from radiant_source import blocks, properties, vector, collision_from_map, encode_collision
from replay_mesh_math import quaternion, pack
import radiant_collision
import map_lighting
import material_channels
import source_atlases
import glass_panes
from vertex_attributes import atlas_vertices, color, coverage_flags
from foliage_material import classify
from cod4_lighting import compile_lighting
from local_paths import COD4, REPLAY

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parents[1]
BASE = REPO / "custom_map_sources/mp_test"
def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def run(command, cwd, log):
    with log.open("w", encoding="utf-8") as output:
        output.write(subprocess.list2cmdline(list(map(str, command))) + "\n")
        output.flush()
        result = subprocess.run(
            list(map(str, command)), cwd=cwd, stdout=output, stderr=subprocess.STDOUT
        )
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}); read {log}")


def bake_native_collision(out, mapid, source, materials, names, surfaces, replay=REPLAY):
    from footstep_data import generate

    package = out / "package"
    package.mkdir(parents=True, exist_ok=True)
    generate(
        package, source, {"materials": materials, "color_images": names}, {"surfaces": surfaces}
    )
    brushes = json.loads((out / "source_collision.json").read_text())
    collision = out / "collision.bin"
    collision.write_bytes(radiant_collision.encode(brushes))
    run(
        [
            sys.executable,
            REPO.parent / "iw8-zonetool/tools/bake_collision.py",
            "--replay",
            replay,
            "--collision",
            collision,
            "--footsteps",
            package / "footsteps.bin",
            "--output",
            out / f"dump/maps/mp/{mapid}.d3dbsp.havok",
        ],
        REPO,
        out / "collision-bake.log",
    )


def numeric_entities(entities):
    result = ['{ 212 "worldspawn" }']
    counts = Counter()
    omitted = Counter()
    for entity in entities:
        cls = entity.get("classname", "")
        if cls == "info_player_start" or cls.startswith("mp_tdm_spawn"):
            if cls not in (
                "info_player_start",
                "mp_tdm_spawn",
                "mp_tdm_spawn_allies_start",
                "mp_tdm_spawn_axis_start",
            ):
                raise ValueError(f"Unsupported spawn class: {cls}")
            origin = vector(entity["origin"])
            angles = vector(entity.get("angles", "0 0 0"))
            fmt = lambda v: " ".join(format(x, ".9g") for x in v)
            result.append(
                '{ 212 "' + cls + '" 709 "' + fmt(origin) + '" 80 "' + fmt(angles) + '" }'
            )
            counts[cls] += 1
        elif cls not in ("worldspawn", "misc_model"):
            omitted[cls] += 1
    for cls in ("mp_tdm_spawn", "mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start"):
        if not counts[cls]:
            raise ValueError(f"Missing required TDM entity: {cls}")
    # Preserve the model-less script entity from the playable v12/v13 package.
    # Spawn markers do not populate the non-player snapshot baseline, and props
    # baked into GfxWorld no longer create runtime entities. Replay rejects
    # subsequent baseline deltas when nextNoDeltaEntity remains zero.
    result.append('{ 212 "script_model" 709 "0 0 16" 80 "0 0 0" }')
    return "\n".join(result) + "\n", dict(counts), dict(omitted)


def build(args):
    if not math.isfinite(args.sun_intensity_scale) or args.sun_intensity_scale <= 0:
        raise ValueError("Sun intensity scale must be finite and positive")
    game = args.cod4.resolve()
    raw = game / "raw"
    reuse = args.reuse_build.resolve() if getattr(args, "reuse_build", None) else None
    source_root = reuse / "map_source" if reuse else game / "map_source"
    source = source_root / "mp_test.map" if reuse else args.source.resolve()
    previous_report = None
    if reuse:
        previous_report = json.loads((reuse / "build_report.json").read_text())
        expected = previous_report["source_sha256"]
        if hashlib.sha256((reuse / "source.map").read_bytes()).hexdigest() != expected or (
            source.read_text(encoding="utf-8-sig")
            != (reuse / "source.map").read_text(encoding="utf-8-sig")
        ):
            raise ValueError("Retained map snapshot does not match its build report")
    text = source.read_text(encoding="utf-8-sig")
    brushes, dependencies = radiant_collision.collect(source, source_root)
    converter = REPO.parent / "iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe"
    for p in (
        (converter, args.replay) if reuse else (game / "bin/cod4map.exe", converter, args.replay)
    ):
        if not p.is_file():
            raise FileNotFoundError(p)
    out = (
        args.out.resolve()
        if args.out
        else BASE / "builds" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    )
    out.mkdir(parents=True, exist_ok=False)
    shutil.copy2(reuse / "source.map" if reuse else source, out / "source.map")
    for name, body in dependencies.items():
        target = out / "map_source" / Path(name).relative_to(source_root)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(body, encoding="utf-8")
    bspbase = out / "compiled/maps/mp/mp_test"
    bspbase.parent.mkdir(parents=True)
    bsp = bspbase.with_suffix(".d3dbsp")
    if reuse:
        print(
            f"Reusing compiled geometry and lighting from {reuse}\nBuild output: {out}", flush=True
        )
        shutil.copy2(reuse / "compiled/maps/mp/mp_test.d3dbsp", bsp)
        shutil.copytree(reuse / "lighting", out / "lighting")
        light_root = out / "lighting"
        worlds = list(light_root.glob("maps/mp/*.replay-world.json"))
        if len(worlds) != 1:
            raise ValueError("Retained build must contain exactly one compiled lighting world")
        lit_world = json.loads(worlds[0].read_text())
        for filename in (
            "lighting.ff",
            "cod4map.log",
            "cod4rad.log",
            "light_link.log",
            "light_export.log",
        ):
            if (reuse / filename).is_file():
                shutil.copy2(reuse / filename, out / filename)
    else:
        print(f"Compiling {source}\nBuild output: {out}", flush=True)
        run(
            [
                game / "bin/cod4map.exe",
                "-platform",
                "pc",
                "-loadFrom",
                out / "map_source" / source.relative_to(source_root),
                bspbase,
            ],
            game,
            out / "cod4map.log",
        )
        compiler_log = (out / "cod4map.log").read_text(errors="replace")
        if any("ERROR:" in line or "is missing" in line for line in compiler_log.splitlines()):
            raise RuntimeError(
                f'Compiler reported an error despite returning success; read {out / "cod4map.log"}'
            )
        if not bsp.is_file():
            raise RuntimeError(f'Compiler produced no BSP; read {out / "cod4map.log"}')
        light_root, lit_world = compile_lighting(game, out, bsp, run, TOOLS)
    lumps, _ = read_bsp(bsp.read_bytes())
    geo = geometry(lumps)
    triangle_hulls = radiant_collision.add_compiled_triangles(brushes, geo["triangle_collision"])
    write_json(out / "source_collision.json", brushes)
    group = geo.get("simple") or geo.get("layered")
    if not group or not group["surfaces"]:
        raise ValueError("Compiled map has no render surfaces")
    material_names = [
        row[0].split(b"\0")[0].decode("ascii") for row in struct.iter_unpack("<64sII", lumps[0])
    ]
    entitytext = lumps[39].rstrip(b"\0").decode("ascii")
    entities = [properties(b) for b in blocks(entitytext)]
    numeric, spawns, omitted = numeric_entities(entities)
    surfaces = []
    models = []
    materials = {}

    def texture(name):
        if name not in materials:
            try:
                image, cutout = material_image(raw, name)
                blended = False
            except BlendedMaterial:
                image, cutout = material_image(raw, name, allow_blend=True)
                blended = True
            materials[name] = {
                **material_channels.describe(raw, name, raw=raw),
                "image": image,
                "alpha_test": cutout,
                "blended": blended,
            }
        return name

    from imported_map_assets import packed_normal
    from source_tjunctions import stitch

    images = Images(game, raw)

    def image(name):
        try:
            return images.get(name)
        except (ValueError, FileNotFoundError) as error:
            raise ValueError(f"Cannot convert source image {name!r}: {error}") from error

    architecture = []
    for surface in lit_world["surfaces"]:
        name = surface["material"].removeprefix("wc/").removeprefix("mc/")
        if name.startswith("sky_"):
            continue
        material = materials[texture(name)]
        classify(name, material, image(material["image"])[0])
        if not material.get("cutout") and not material.get("blended"):
            architecture.append(surface)
    junction_report = stitch(architecture)
    print("Source edge stitching: " + str(junction_report), flush=True)

    for surface in lit_world["surfaces"]:
        name = surface["material"].removeprefix("wc/").removeprefix("mc/")
        if name.startswith("sky_"):
            continue
        vertices = [
            {
                "position": v["position"],
                "uv": v["uv"],
                "normal": packed_normal(v["normal"], v["tangent"], v["binormal_sign"]),
                "color": color(v),
                "baked_uv": v["lightmap_uv"],
                "baked_index": surface["lightmap"],
            }
            for v in surface["vertices"]
        ]
        surfaces.append(
            {"vertices": vertices, "indices": surface["indices"], "material": texture(name)}
        )
    world_surfaces = len(surfaces)
    for e in entities:
        if e.get("classname") != "misc_model":
            continue
        name = e.get("model", "").removeprefix("xmodel/")
        origin = vector(e.get("origin", "0 0 0"))
        angles = vector(e.get("angles", "0 0 0"))
        scale = float(e.get("modelscale", "1"))
        props = load_model(raw, name)
        omitted_surfaces = []
        shadow_helpers = []
        included = 0
        for s in props:
            try:
                texture(s["material"])
            except ShadowOnlyMaterial as error:
                print(f"INFO: {name}: {error}", flush=True)
                shadow_helpers.append(s["material"])
                continue
            except BlendedMaterial as error:
                if not args.omit_blended:
                    raise
                print(f"WARNING: {name}: omitting {error}", flush=True)
                omitted_surfaces.append(
                    {"material": s["material"], "triangles": len(s["indices"]) // 3}
                )
                continue
            surfaces.append(transformed(s, origin, angles, scale))
            included += 1
        if not included:
            raise ValueError(f"{name}: no supported opaque model surfaces remain")
        models.append(
            {
                "model": name,
                "origin": origin,
                "angles": angles,
                "scale": scale,
                "surfaces": len(props),
                "triangles": sum(len(s["indices"]) // 3 for s in props),
                "included_surfaces": included,
                "omitted_blended_surfaces": omitted_surfaces,
                "excluded_shadow_helpers": shadow_helpers,
            }
        )
    if len(surfaces) + 6 > 4096:
        raise ValueError("More than 4096 render surfaces")
    if any(abs(x) > 100000 for s in surfaces for v in s["vertices"] for x in v["position"]):
        raise ValueError("Vertex outside supported world bounds")
    foliage = []
    for name, m in materials.items():
        classify(name, m, image(m["image"])[0])
        if m.get("cutout"):
            foliage.append({"material": name, "method": "gpu_alpha_test", "threshold": 128})
    has_cutout = any(m.get("cutout") for m in materials.values())
    has_glass = any(m.get("blended") for m in materials.values())
    from decal_geometry import separate

    decal_report = separate(surfaces, materials)
    print("Decal separation: " + str(decal_report), flush=True)
    surfaces, panes = glass_panes.prepare(surfaces, materials)
    brushes, glass_collision_removed = glass_panes.remove_static_collision(brushes, panes)
    write_json(out / "source_collision.json", brushes)
    atlases, keys, columns, cell = source_atlases.pack(materials, surfaces, image)
    atlas, normal_atlas, response_atlas = atlases
    names = [key[0] for key in keys]
    tiles = {key: index for index, key in enumerate(keys)}
    sky = image(args.sky)
    if len(sky) != 6:
        raise ValueError("Sky must be a six-face CoD4 cubemap image")
    skybase = len(keys)
    for i, im in enumerate(sky, start=skybase):
        atlas.paste(
            im.resize((cell, cell), Image.Resampling.LANCZOS),
            ((i % columns) * cell, (i // columns) * cell),
        )
    lightmaps = map_lighting.pack_coefficients(
        atlas,
        normal_atlas,
        response_atlas,
        light_root,
        lit_world,
        (len(keys) + 6 + columns - 1) // columns,
        cell,
    )
    for s in surfaces:
        material = materials[s.pop("material")]
        tile = tiles[material_channels.tile_key(material)]
        s["materialParameters"] = material["environment"]
        kind = 3 if material.get("cutout") else (2 if material.get("blended") else 0)
        s["materialIndex"] = 1 if kind == 3 else ((2 if has_cutout else 1) if kind == 2 else 0)
        s["vertices"] = atlas_vertices(
            s["vertices"],
            tile,
            kind
            + coverage_flags(material)
            + (32 if material.get("normal_image") else 0)
            + (64 if material.get("specular_image") else 0),
            lightmaps,
        )
    for i, s in enumerate(sky_surfaces()):
        for v in s["vertices"]:
            v["lightmapUV"] = [skybase + i + 0.25, 1.25]
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
    folder = out / "dump/maps/mp"
    folder.mkdir(parents=True)
    (folder / "mp_test.d3dbsp.ents").write_text(entitytext, encoding="ascii")
    (out / "dump/mp_test_iw8_ents.txt").write_text(numeric, encoding="ascii")
    write_json(
        folder / "mp_test.d3dbsp.render.json",
        {
            "schema": 1,
            "source_sha256": hashlib.sha256(bsp.read_bytes()).hexdigest(),
            "material": "w/mw120r_test",
            "materialDefinition": "mp_test.d3dbsp.material.json",
            "additionalMaterials": [
                {
                    "schema": 1,
                    "material": "w/mw120r_mp_test_" + kind,
                    "materialDefinition": "mp_test.d3dbsp." + kind + ".material.json",
                }
                for kind, enabled in [("foliage", has_cutout), ("glass", has_glass), ("sky", True)]
                if enabled
            ],
            "surfaces": surfaces,
        },
    )
    material = json.loads((BASE / "dump/maps/mp/mp_test.d3dbsp.material.json").read_text())
    material["imageDefinitions"] = []
    for channel, target in enumerate(atlases):
        pixels = target.tobytes()
        image_name = f"mw120r/mp_test_{channel}_" + hashlib.sha256(pixels).hexdigest()[:16]
        filename = f"mp_test_atlas_{channel}.rgba"
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
    write_json(folder / "mp_test.d3dbsp.material.json", material)
    atlas.save(out / "atlas.png")
    direction, sun_color = map_lighting.sun(entities[0])
    from map_presentation import prepare

    prepare(folder, "mp_test", columns, direction, sun_color, sky)
    shader = "#define MAP_SOURCE_CHANNELS 1\n" + (
        (TOOLS / "map_surface_realtime.hlsl").read_text().replace("ATLAS_COLUMNS", str(columns))
    )
    if args.lighting_profile == "source":
        shader = "#define MAP_SOURCE_SUN_MASK 1\n" + shader
    (out / "authored_map.hlsl").write_text(shader)
    run(
        [
            sys.executable,
            TOOLS / "compile_graybox_shader.py",
            "--source",
            out / "authored_map.hlsl",
            "--target-root",
            out,
        ],
        REPO,
        out / "shader.log",
    )
    from glass_material import create

    for kind, enabled in [("foliage", has_cutout), ("glass", has_glass), ("sky", True)]:
        if enabled:
            create(folder, "mp_test.d3dbsp", "mp_test", kind)
    package = out / "package"
    from native_lightgrid import convert as convert_lightgrid

    grid_sources = list((light_root / "maps/mp").glob("*.d3dbsp.lightgrid.json"))
    if len(grid_sources) == 1:
        source_map = grid_sources[0].name.removesuffix(".d3dbsp.lightgrid.json")
        convert_lightgrid(light_root, source_map, folder / "mp_test.d3dbsp.gpulightgrid.bin")
    bake_native_collision(
        out,
        "mp_test",
        REPO / "custom_map_sources/mp_test/extracted",
        materials,
        names,
        surfaces,
        args.replay,
    )
    run(
        [
            converter,
            "fromdump",
            out / "dump",
            "mp_test",
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
    manifest = json.loads((package / "manifest.json").read_text())
    manifest.update(
        title="CoD4 mp_test - Radiant Build",
        description="Radiant world and static CoD4 props. Convex brush collision; Team Deathmatch; no bot navigation.",
        collision="convex-v3",
        glass="panes-v2",
        visibility="all-visible-v1",
        world="replay-1.20-native-v1",
    )
    glass_data = glass_panes.encode(panes, surfaces)
    glass_panes.validate(glass_data)
    (package / "glass.bin").write_bytes(glass_data)
    write_json(package / "manifest.json", manifest)
    (package / "collision.bin").write_bytes(radiant_collision.encode(brushes))
    from map_presentation import preview

    preview(package, "mp_test")
    spatial_grid = None
    run([converter, "validate-package", package, "mp_test"], REPO, out / "validate.log")
    run(
        [
            sys.executable,
            TOOLS / "verify_replay_map_layout.py",
            "--game",
            args.replay,
            "--package",
            package,
            "--out",
            out / "layout.json",
        ],
        REPO,
        out / "layout.log",
    )
    acts = REPO.parent / "atian-cod-tools/build/bin/Release/acts.exe"
    acts_report = out / "acts-validation.json"
    if acts.is_file():
        run(
            [
                sys.executable,
                TOOLS / "validate_replay_package_acts.py",
                "--acts",
                acts,
                "--game",
                args.replay,
                "--package",
                package,
                "--map",
                "mp_test",
                "--output-dir",
                out / "acts-validation",
                "--log",
                out / "acts-loader.log",
                "--out",
                acts_report,
            ],
            REPO,
            out / "acts-validation-wrapper.log",
        )
    report = {
        "source": str(source),
        "source_sha256": hashlib.sha256((out / "source.map").read_bytes()).hexdigest(),
        "reused_build": str(reuse) if reuse else None,
        "compiled_bsp_sha256": hashlib.sha256(bsp.read_bytes()).hexdigest(),
        "package": str(package),
        "world_surfaces": world_surfaces,
        "models": models,
        "spawns": spawns,
        "omitted_non_tdm_entities": omitted,
        "collision_hulls": len(brushes),
        "collision_triangle_hulls": triangle_hulls,
        "snapshot_baseline_anchor": "model-less script_model at 0 0 16",
        "materials": materials,
        "color_images": names,
        "source_channels": {
            "version": 1,
            "normal_materials": sum(bool(m.get("normal_image")) for m in materials.values()),
            "specular_materials": sum(bool(m.get("specular_image")) for m in materials.values()),
            "directional_lightmaps": True,
        },
        "source_edge_stitching": junction_report,
        "spatial_ambient": spatial_grid,
        "sky": args.sky,
        "lightmaps": len(lightmaps),
        "lighting": "CoD4 baked lightmaps and model shadows",
        "foliage": foliage,
        "surfaces": len(surfaces),
        "vertices": sum(len(s["vertices"]) for s in surfaces),
        "triangles": sum(len(s["indices"]) // 3 for s in surfaces),
        "game_tested": False,
        "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in package.iterdir()},
    }
    if acts_report.is_file():
        report["native_loader_validation"] = json.loads(acts_report.read_text())
    else:
        report["native_loader_validation"] = {
            "success": False,
            "skipped": True,
            "reason": "AtianCodToolsCLI was not available",
        }
    from footstep_data import generate

    report["footsteps"] = generate(
        package, REPO / "custom_map_sources/mp_test/extracted", report, {"surfaces": surfaces}
    )
    manifest = json.loads((package / "manifest.json").read_text())
    manifest["footsteps"] = "triangles-v1"
    write_json(package / "manifest.json", manifest)
    report["files"] = {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in package.iterdir() if p.is_file()
    }
    write_json(out / "build_report.json", report)
    if args.deploy:
        shell = shutil.which("pwsh") or shutil.which("powershell")
        if not shell:
            raise RuntimeError("PowerShell is required for deployment")
        run(
            [
                shell,
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                TOOLS / "deploy_custom_map.ps1",
                "-PackageDir",
                package,
            ],
            REPO,
            out / "deploy.log",
        )
    print(json.dumps(report, indent=2))
    return out


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cod4", type=Path, default=COD4)
    parser.add_argument("--source", type=Path, default=COD4 / "map_source/mp_test.map")
    parser.add_argument("--replay", type=Path, default=REPLAY)
    parser.add_argument("--sky", default="chechnya_ft")
    parser.add_argument(
        "--lighting-profile", choices=("source", "aniyah-incursion"), default="source"
    )
    parser.add_argument("--sun-intensity-scale", type=float, default=6.0)
    parser.add_argument("--out", type=Path)
    parser.add_argument(
        "--reuse-build",
        type=Path,
        help="Reconvert retained compiled geometry and lighting without running Radiant or CoD4 compilers",
    )
    parser.add_argument("--deploy", action="store_true")
    parser.add_argument(
        "--omit-blended",
        action="store_true",
        help="Explicitly omit glass/decal blend surfaces, recording every omission in the build report",
    )
    parser.add_argument(
        "--foliage-mask-size",
        type=int,
        choices=(128, 256, 512, 1024),
        default=128,
        help="Maximum alpha-mask dimension for baked cutout foliage",
    )
    try:
        build(parser.parse_args())
    except (ValueError, RuntimeError, FileNotFoundError) as error:
        print(f"BUILD FAILED: {error}", file=sys.stderr)
        sys.exit(1)
