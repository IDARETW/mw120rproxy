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


def build_candidate(build, indirect_ev=0.0, unbaked_ev=0.0):
    defines = exposure_defines(indirect_ev, unbaked_ev)
    build = build.resolve()
    package = build / "package"
    manifest = json.loads((package / "manifest.json").read_text())
    mapid = manifest["id"]
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", mapid):
        raise ValueError("Invalid map id")
    source = find_dump(build, mapid)
    report = json.loads((source / "build_report.json").read_text())
    cell = report["atlas_cell"]
    if cell not in (32, 64, 128, 256, 512, 1024):
        raise ValueError("Unsupported atlas cell size")
    out = build.parent / (datetime.now().strftime("%Y%m%d-%H%M%S-%f") + "-native-lighting")
    out.mkdir()
    shutil.copytree(source / "dump", out / "dump")
    folder = out / "dump/maps/mp"
    stem = mapid + ".d3dbsp"
    shader = defines + (
        (TOOLS / "map_surface_realtime.hlsl")
        .read_text()
        .replace("ATLAS_COLUMNS", str(4096 // cell))
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
        [converter, "fromdump", out / "dump", mapid, "-o", destination, "--stored"],
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
        "ambient.bin",
        "preview.rgba",
    ):
        path = package / name
        if path.is_file():
            shutil.copy2(path, destination / name)
            preserved[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            if hashlib.sha256((destination / name).read_bytes()).hexdigest() != preserved[name]:
                raise ValueError("Sidecar copy verification failed: " + name)
    manifest["lighting"] = "native-shadow-candidate-v2"
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
    report.update(
        package=str(destination),
        previous_build=str(build),
        source_build=str(source),
        game_tested=False,
        validated=True,
        installed=False,
        lighting="native-shadow-candidate-v2",
        indirect_ev=indirect_ev,
        unbaked_ev=unbaked_ev,
        sky_surfaces=sky_count,
        preserved_sidecars=preserved,
        files={
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in destination.iterdir()
            if p.is_file()
        },
        lighting_limits=[
            "Live sun-shadow reception and screen-space AO are enabled for opaque surfaces.",
            "Glass and foliage retain baked lighting until they have matching depth prepasses.",
            "Native objects still use the map-wide fallback probe; spatial light-grid conversion is pending.",
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
    build_candidate(args.build, args.indirect_ev, args.unbaked_ev)
