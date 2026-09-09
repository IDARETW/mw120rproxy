"""Build a separate native-shadow lighting candidate from an existing map build.

The source package and installed game are never changed. Geometry order and all
gameplay sidecars are retained, including door poses and glass pane indices.
"""

import argparse
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import sys

from build_mp_test import REPO, REPLAY, TOOLS, run, write_json
from glass_material import create


def find_dump(build, mapid):
    seen = set()
    for _ in range(32):
        build = build.resolve()
        if build in seen:
            raise ValueError("Circular build history")
        seen.add(build)
        if (build / f"dump/maps/mp/{mapid}.d3dbsp.render.json").is_file():
            return build
        report = json.loads((build / "build_report.json").read_text())
        previous = report.get("previous_build") or report.get("source_build")
        if not previous:
            break
        build = Path(previous)
    raise ValueError("No retained geometry in this build's history")


def exposure_defines(indirect_ev, unbaked_ev):
    if any(not math.isfinite(v) or not -4 <= v <= 4 for v in (indirect_ev, unbaked_ev)):
        raise ValueError("Lighting exposure must be finite and between -4 and +4 EV")
    return f"#define MAP_INDIRECT_GAIN {2 ** indirect_ev:.9g}\n#define MAP_UNBAKED_GAIN {2 ** unbaked_ev:.9g}\n"


def build_candidate(
    build,
    indirect_ev=0.0,
    unbaked_ev=0.0,
    source_build=None,
    preserve_lighting=False,
    refresh_source_textures=False,
    output_root=None,
    lighting_profile="aniyah-incursion",
    sun_intensity_scale=1.0,
):
    if lighting_profile not in ("aniyah-incursion", "source"):
        raise ValueError("Invalid lighting profile")
    if not math.isfinite(sun_intensity_scale) or sun_intensity_scale <= 0:
        raise ValueError("Sun intensity scale must be finite and positive")
    defines = exposure_defines(indirect_ev, unbaked_ev)
    build = build.resolve()
    package = build / "package"
    manifest = json.loads((package / "manifest.json").read_text())
    mapid = manifest["id"]
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", mapid):
        raise ValueError("Invalid map id")
    source = find_dump(source_build or build, mapid)
    report = json.loads((source / "build_report.json").read_text())
    shader_path = next(
        (source / n for n in ("map.hlsl", "authored_map.hlsl") if (source / n).is_file()), None
    )
    cell = report.get("atlas_cell")
    if cell is None and shader_path:
        columns = re.search(r"cellSize\s*=\s*4096\.0\s*/\s*(\d+)", shader_path.read_text())
        if columns and int(columns[1]) in (4, 8, 16, 32, 64, 128):
            cell = 4096 // int(columns[1])
    if cell not in (32, 64, 128, 256, 512, 1024):
        raise ValueError("Unsupported atlas cell size")
    parent = Path(output_root).resolve() / mapid if output_root else build.parent
    out = parent / (datetime.now().strftime("%Y%m%d-%H%M%S-%f") + "-native-lighting")
    out.mkdir(parents=True)
    shutil.copytree(source / "dump", out / "dump")
    folder = out / "dump/maps/mp"
    stem = mapid + ".d3dbsp"
    if lighting_profile == "source":
        from map_lighting import sun, sun_settings
        from radiant_source import blocks, properties

        # Re-read the original worldspawn; older lighting JSON used a clipped
        # intensity and cannot recover the original source light energy.
        entities = [properties(b) for b in blocks((folder / (stem + ".ents")).read_text())]
        worldspawn = next(e for e in entities if e.get("classname") == "worldspawn")
        write_json(folder / (stem + ".lighting.json"), sun_settings(*sun(worldspawn)))
    texture_refresh = None
    if refresh_source_textures:
        from build_mp_test import COD4
        from cod4_assets import Images
        from imported_map_assets import safe_asset
        from prepare_textured_mp_test import decode_iwi
        from source_atlases import refresh

        source_root = Path(report["source"])
        fallback = Images(COD4, COD4 / "raw")

        def image(name):
            path = safe_asset(source_root, "images", name, ".iwi")
            return decode_iwi(path.read_bytes()) if path.is_file() else fallback.get(name)

        texture_refresh = refresh(folder, mapid, report, 4096 // cell, image)
    shader = defines + (
        (TOOLS / "map_surface_realtime.hlsl")
        .read_text()
        .replace("ATLAS_COLUMNS", str(4096 // cell))
    )
    if report.get("source_channels", {}).get("version") == 1:
        shader = "#define MAP_SOURCE_CHANNELS 1\n" + shader
    if lighting_profile == "source":
        shader = "#define MAP_SOURCE_SUN_MASK 1\n" + shader
    if preserve_lighting:
        if shader_path is None:
            raise ValueError("No retained color shader; cannot preserve this map's lighting")
        shader = shader_path.read_text()
        if lighting_profile != "source":
            shader = shader.replace(
                "#define MAP_SOURCE_SUN_MASK 1", "#define MAP_SOURCE_SUN_MASK 0"
            )
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
    mesh_path = folder / (stem + ".render.json")
    mesh = json.loads(mesh_path.read_text())
    materials = mesh.setdefault("additionalMaterials", [])
    for kind in ("foliage", "glass"):
        if (folder / (stem + "." + kind + ".material.json")).exists():
            create(folder, stem, mapid, kind)
    # The sky stays in the opaque surface range but has no depth/shadow
    # techniques. Keep every surface index stable for doors and glass.
    sky = create(folder, stem, mapid, "sky")
    existing = next((i + 1 for i, m in enumerate(materials) if m["material"] == sky["material"]), 0)
    if not existing:
        materials.append(sky)
        existing = len(materials)
    sky_count = 0
    for surface in mesh["surfaces"]:
        if int(surface["vertices"][0]["lightmapUV"][1]) % 4 == 1:
            surface["materialIndex"] = existing
            surface["renderClass"] = "sky"
            sky_count += 1
    if not sky_count:
        raise ValueError("No sky surfaces found")
    with mesh_path.open("w") as stream:
        json.dump(mesh, stream, separators=(",", ":"))
    del mesh
    converter = REPO / "iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe"
    destination = out / "package"
    run(
        [
            converter,
            "fromdump",
            out / "dump",
            mapid,
            "-o",
            destination,
            "--stored",
            "--lighting-profile",
            lighting_profile,
            "--sun-intensity-scale",
            str(sun_intensity_scale),
        ],
        REPO,
        out / "convert.log",
    )
    preserved = {}
    for name in (
        "collision.bin",
        "doors.bin",
        "glass.bin",
        "ladders.bin",
        "footsteps.bin",
        "preview.rgba",
    ):
        path = package / name
        if path.is_file():
            shutil.copy2(path, destination / name)
            preserved[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            if hashlib.sha256((destination / name).read_bytes()).hexdigest() != preserved[name]:
                raise ValueError("Sidecar copy verification failed: " + name)
    converted_manifest = json.loads((destination / "manifest.json").read_text())
    manifest["lighting_profile"] = converted_manifest["lighting_profile"]
    manifest["sun_intensity_scale"] = converted_manifest["sun_intensity_scale"]
    manifest["lighting_runtime_override"] = False
    manifest.pop("ambient", None)
    manifest.pop("ambient_grid", None)
    if not preserve_lighting:
        manifest["lighting"] = lighting_profile + "-native-sun-v2"
    manifest["sun_visibility"] = (
        "source-static-and-native-dynamic-v1"
        if "#define MAP_SOURCE_SUN_MASK 1" in shader
        else "native-tile-classification-v1" if "lightingTiles" in shader else "legacy-unclassified"
    )
    manifest["depth_coverage"] = "atlas-prepass-v1"
    write_json(destination / "manifest.json", manifest)
    run([converter, "validate-package", destination, mapid], REPO, out / "validate.log")
    run(
        [
            sys.executable,
            TOOLS / "verify_replay_map_layout.py",
            "--game",
            REPLAY,
            "--package",
            destination,
            "--map",
            mapid,
            "--out",
            out / "layout.json",
        ],
        REPO,
        out / "layout.log",
    )
    acts = REPO / "external/atian-cod-tools/build/bin/Release/acts.exe"
    if acts.is_file():
        from validate_replay_package_acts import validate

        native_validation = validate(
            acts, REPLAY, destination, mapid, out / "acts-validation", out / "acts-validation.log"
        )
        write_json(out / "acts-validation.json", native_validation)
    else:
        native_validation = None
    report.update(
        package=str(destination),
        atlas_cell=cell,
        previous_build=str(build),
        source_build=str(source),
        game_tested=False,
        validated=True,
        installed=False,
        lighting=manifest.get("lighting"),
        depth_coverage="atlas-prepass-v1",
        native_validation=native_validation,
        indirect_ev=indirect_ev,
        unbaked_ev=unbaked_ev,
        sky_surfaces=sky_count,
        preserved_sidecars=preserved,
        texture_refresh=texture_refresh,
        files={
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in destination.iterdir()
            if p.is_file()
        },
        lighting_limits=[
            "Live sun-shadow reception is enabled for opaque surfaces; indirect occlusion comes from source lightmaps.",
            "Masked surfaces have matching depth/normal coverage; translucent glass does not write opaque depth.",
            "Native per-object light grids are not generated; no runtime ambient probe override is used.",
        ],
    )
    write_json(out / "build_report.json", report)
    print(
        json.dumps(
            {
                "package": str(destination),
                "sky_surfaces": sky_count,
                "game_tested": False,
                "installed": False,
            },
            indent=2,
        ),
        flush=True,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument(
        "--lighting-profile", choices=("aniyah-incursion", "source"), default="aniyah-incursion"
    )
    parser.add_argument(
        "--sun-intensity-scale",
        type=float,
        default=1.0,
        help="Multiply the selected native sun intensity; does not change indirect light.",
    )
    parser.add_argument(
        "--output-root", type=Path, help="Optional build output drive or directory."
    )
    parser.add_argument(
        "--refresh-source-textures",
        action="store_true",
        help="Regenerate material atlas tiles and mipmaps from retained source images.",
    )
    parser.add_argument(
        "--preserve-lighting",
        action="store_true",
        help="Retain the compiled-color shader source; the converter still applies its default sun profile.",
    )
    parser.add_argument(
        "--source-build",
        type=Path,
        help="Retained dump build, when an older repack did not record its source history.",
    )
    parser.add_argument(
        "--indirect-ev",
        type=float,
        default=0.0,
        help="Exposure of authored baked indirect light; -1 halves it.",
    )
    parser.add_argument(
        "--unbaked-ev",
        type=float,
        default=0.0,
        help="Exposure of fallback fill on imported props without lightmaps.",
    )
    args = parser.parse_args()
    build_candidate(
        args.build,
        args.indirect_ev,
        args.unbaked_ev,
        args.source_build,
        args.preserve_lighting,
        args.refresh_source_textures,
        args.output_root,
        args.lighting_profile,
        args.sun_intensity_scale,
    )
