#!/usr/bin/env python3
"""Import maps and static meshes into IW8 1.20 Replay packages, entirely offline."""

import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path

from mapimport.bsp import read_q2, read_q3
from mapimport.cod import read_cod
from mapimport.core import read_bytes, sha256, write_json
from mapimport.mesh import read_gltf, read_obj
from mapimport.replay import prepare, preview
from mapimport.source import read_source

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TOOLS = ROOT.parent / "mw120rproxy/mw120rproxy/tools"
FORMATS = [
    {
        "id": "iw3",
        "input": "OpenAssetTools Replay world/collision dump or FF via configured Unlinker",
        "geometry": True,
        "collision": True,
    },
    {
        "id": "iw4",
        "input": "OpenAssetTools IW8 world/collision dump or FF via extended Unlinker",
        "geometry": True,
        "collision": True,
    },
    {
        "id": "iw5",
        "input": "OpenAssetTools IW8 world/collision dump or FF via extended Unlinker",
        "geometry": True,
        "collision": True,
    },
    {"id": "q2", "input": "IBSP 38 (.bsp)", "geometry": True, "collision": True},
    {
        "id": "q3",
        "input": "IBSP 46 (.bsp), including quadratic patches",
        "geometry": True,
        "collision": True,
    },
    {"id": "ql", "input": "IBSP 47 (.bsp)", "geometry": True, "collision": True},
    {
        "id": "source",
        "input": "VBSP 19/20 (.bsp), including displacements",
        "geometry": True,
        "collision": True,
    },
    {
        "id": "obj",
        "input": "Wavefront .obj with .mtl and color images",
        "geometry": True,
        "collision": "triangle prisms",
    },
    {
        "id": "gltf",
        "input": "glTF 2.0 .gltf/.glb static TRIANGLES",
        "geometry": True,
        "collision": "triangle prisms",
    },
]


def detect(path):
    if path.is_dir():
        return "cod-dump"
    if path.suffix.lower() in (".zip", ".pk3", ".iwd", ".pak"):
        return "archive"
    data = read_bytes(path)
    if data[:4] == b"IBSP":
        if len(data) < 8:
            raise ValueError("Truncated IBSP header")
        version = struct.unpack_from("<i", data, 4)[0]
        engine = {38: "q2", 46: "q3", 47: "ql"}.get(version)
        if not engine:
            raise ValueError(
                f"Unsupported IBSP version {version}; refusing to read it as another engine"
            )
        return engine
    if data[:4] == b"VBSP":
        return "source"
    if data[:4] == b"glTF" or path.suffix.lower() == ".gltf":
        return "gltf"
    if path.suffix.lower() == ".obj":
        return "obj"
    if path.suffix.lower() == ".ff":
        return "cod-ff"
    raise ValueError(
        "Unsupported source. Use formats to list implemented readers. Raw x64-zt GfxWorld dumps, Source 2, Unreal packages and raw CoD BSP are not accepted as generic meshes."
    )


def verify_package(package, mapid):
    manifest = json.loads((package / "manifest.json").read_text())
    if (
        manifest.get("schema") != 1
        or manifest.get("id") != mapid
        or "tdm" not in manifest.get("gametypes", [])
    ):
        raise ValueError("Invalid package manifest")
    files = {}
    for name in (
        mapid,
        "srv_" + mapid,
        "eng_" + mapid,
        "ww_" + mapid,
        "techsets_" + mapid,
    ):
        path = package / (name + ".ff")
        data = read_bytes(path)
        if len(data) < 140 or data[:8] != b"IWffc100" or data[136:140] != b"\x01IWC":
            raise ValueError("Invalid stored Replay fastfile")
        # Native validate-package also checks all header fields and stream sizes.
        files[path.name] = {"bytes": len(data), "sha256": sha256(path)}
    return files


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("map", help="Target lower-case mp_<name>; independent of the source map name")
    p.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="Fresh build directory; final files go in package/",
    )
    p.add_argument(
        "--format",
        choices=[x["id"] for x in FORMATS],
        help="Verify detected source format",
    )
    p.add_argument("--source-map", help="Select a map from a CoD dump or a map archive")
    p.add_argument(
        "--asset-root",
        type=Path,
        action="append",
        default=[],
        help="Repeat for texture directories, PK3/IWD/ZIP or PAK archives",
    )
    p.add_argument(
        "--spawn",
        type=float,
        nargs=3,
        action="append",
        default=[],
        metavar=("X", "Y", "Z"),
        help="Additional spawn in OUTPUT units/Z-up coordinates",
    )
    p.add_argument(
        "--scale",
        type=float,
        help="Positive scale; default 39.37007874 for glTF meters, 1 otherwise",
    )
    p.add_argument(
        "--up-axis",
        choices=("z", "y"),
        default="z",
        help="OBJ source up axis (glTF already specifies Y-up)",
    )
    p.add_argument("--patch-steps", type=int, default=6)
    p.add_argument(
        "--graybox",
        action="store_true",
        help="Use a stock material, without Replay shader template dependencies",
    )
    p.add_argument(
        "--strict-textures",
        action="store_true",
        help="Fail if any requested texture is absent or unsupported",
    )
    p.add_argument("--title", help="Map selector title")
    p.add_argument(
        "--replay",
        type=Path,
        required=True,
        help="Replay 1.20.4.7623265 executable used for offline collision baking",
    )
    p.add_argument("--credit", default="", help="Source author/license attribution for the package")
    p.add_argument("--writer", type=Path, default=ROOT / "xmake-out/x64/Release/iw8-zonetool.exe")
    p.add_argument(
        "--replay-tools",
        type=Path,
        default=DEFAULT_TOOLS if DEFAULT_TOOLS.is_dir() else None,
    )
    p.add_argument(
        "--unlinker",
        type=Path,
        help="Offline OpenAssetTools Unlinker with IW8 map exporters",
    )
    p.add_argument(
        "--search-path",
        type=Path,
        action="append",
        default=[],
        help="CoD FF dependency search path; repeat as needed",
    )
    return p


def run(args):
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", args.map):
        raise ValueError("Map id must match mp_[a-z0-9_]{1,60}")
    source = args.input.resolve()
    out = args.output.resolve()
    writer = args.writer.resolve()
    if not source.exists():
        raise FileNotFoundError(source)
    if (
        out == source
        or source.is_relative_to(out)
        or (source.is_dir() and out.is_relative_to(source))
    ):
        raise ValueError("Source and output must not contain each other")
    if out.exists():
        raise ValueError(
            "Output already exists; choose a fresh directory to preserve completed and failed builds"
        )
    if not writer.is_file():
        raise FileNotFoundError(f"Build the native writer with XMake first: {writer}")
    if args.graybox and args.strict_textures:
        raise ValueError("--graybox and --strict-textures are mutually exclusive")
    if args.up_axis != "z" and source.suffix.lower() != ".obj":
        raise ValueError("--up-axis is only for OBJ; other readers define source axes")
    if args.replay_tools:
        args.replay_tools = args.replay_tools.resolve()
    args.asset_root = [p.resolve() for p in args.asset_root]
    if source.is_dir():
        args.asset_root.insert(0, source)
    else:
        args.asset_root.insert(0, source.parent)
    out.mkdir(parents=True)
    report = {
        "schema": 1,
        "status": "running",
        "source": str(source),
        "map": args.map,
        "game_tested": False,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "writer": str(writer),
        "writer_sha256": sha256(writer),
    }
    if source.is_file():
        report["source_sha256"] = sha256(source)
    write_json(out / "report.json", report)
    try:
        kind = detect(source)
        if kind == "archive":
            from mapimport.assets import Assets
            from mapimport.core import Scene, safe_path

            inventory = Scene("archive")
            assets = Assets([source], inventory)
            candidates = [
                name for name in assets.names() if Path(name).suffix.lower() in (".bsp", ".ff")
            ]
            if args.source_map:
                candidates = [
                    name
                    for name in candidates
                    if Path(name).stem == args.source_map or name == args.source_map
                ]
            if len(candidates) != 1:
                raise ValueError(
                    f"Archive contains {len(candidates)} matching maps; select one with --source-map. Candidates: {candidates[:20]}"
                )
            member = candidates[0]
            target = safe_path(out / "source-input", member)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(assets.read(member))
            args.asset_root.insert(0, source)
            report["archive_member"] = member
            report["archive_sha256"] = sha256(source)
            source = target
            kind = detect(source)
        if kind == "cod-ff":
            unlinker = args.unlinker
            if unlinker is None:
                unlinker = (
                    DEFAULT_TOOLS / "_vendor/OpenAssetTools/build/bin/Release_x86/Unlinker.exe"
                )
            unlinker = unlinker.resolve()
            if not unlinker.is_file():
                raise FileNotFoundError("Specify --unlinker for raw CoD fastfiles")
            dump = out / "extracted"
            dump.mkdir()
            command = [
                str(unlinker),
                "--no-color",
                "--image-format",
                "DDS",
                "--model-format",
                "OBJ",
                "-o",
                str(dump),
            ]
            if args.search_path:
                command.extend(
                    (
                        "--search-path",
                        ";".join(str(p.resolve()) for p in args.search_path),
                    )
                )
            command.append(str(source))
            with (out / "unlinker.log").open("w") as log:
                subprocess.run(
                    command,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=True,
                    timeout=600,
                )
            report["unlinker"] = {"path": str(unlinker), "sha256": sha256(unlinker)}
            # OAT may nest output under the zone name. Select only a unique map directory.
            candidates = list(dump.rglob("*.iw8-world.json")) + list(
                dump.rglob("*.replay-world.json")
            )
            if not candidates:
                raise ValueError(
                    "Unlinker produced no map world; inspect unlinker.log and install the matching map exporters"
                )
            roots = set()
            for file in candidates:
                for parent in file.parents:
                    if parent.name == "maps":
                        roots.add(parent.parent)
                        break
            if len(roots) != 1:
                raise ValueError("Unlinker output contains ambiguous map roots")
            root = next(iter(roots))
            args.asset_root.insert(0, root)
            scene = read_cod(root, args.source_map, args.format)
        elif kind == "cod-dump":
            scene = read_cod(source, args.source_map, args.format)
        else:
            if args.format and args.format != kind:
                raise ValueError(f"Source signature is {kind}, not requested {args.format}")
            scene = {
                "q2": read_q2,
                "q3": lambda p: read_q3(p, args.patch_steps),
                "ql": lambda p: read_q3(p, args.patch_steps),
                "source": read_source,
                "obj": read_obj,
                "gltf": read_gltf,
            }[kind](source)
        scale = (
            args.scale
            if args.scale is not None
            else 39.37007874015748 if scene.engine == "gltf" else 1.0
        )
        scene.transform(scale, args.up_axis)
        scene.validate()
        if not scene.hulls:
            scene.triangle_collision()
        scene.validate()
        report.update(engine=scene.engine, scale=scale, up_axis=args.up_axis)
        textures = prepare(scene, out, args.map, args)
        # Embedded glTF image bytes become an atlas; don't serialize Python bytes.
        normalized = asdict(scene)
        for m in normalized["materials"].values():
            if "image_bytes" in m:
                raw = m.pop("image_bytes")
                m["embedded_image_sha256"] = __import__("hashlib").sha256(raw).hexdigest()
        write_json(out / "scene.json", normalized)
        preview(scene, out / "preview.png")
        package = out / "package"
        package.mkdir()
        with (out / "collision-bake.log").open("w") as log:
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/bake_collision.py"),
                    "--replay",
                    str(args.replay),
                    "--collision",
                    str(out / "collision.bin"),
                    "--output",
                    str(out / f"dump/maps/mp/{args.map}.d3dbsp.havok"),
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=300,
            )
        with (out / "writer.log").open("w") as log:
            subprocess.run(
                [
                    str(writer),
                    "fromdump",
                    str(out / "dump"),
                    args.map,
                    "-o",
                    str(package),
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=300,
            )
        shutil.copy2(out / "collision.bin", package / "collision.bin")
        manifest = json.loads((package / "manifest.json").read_text())
        manifest.update(
            title=args.title or args.map,
            description=f"Offline {scene.engine} static map conversion. " + args.credit,
            collision="convex-v2",
            source_engine=scene.engine,
            game_tested=False,
        )
        write_json(package / "manifest.json", manifest)
        with (out / "validate.log").open("w") as log:
            subprocess.run(
                [str(writer), "validate-package", str(package), args.map],
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=120,
            )
        files = verify_package(package, args.map)
        files.update(
            {
                p.name: {"bytes": p.stat().st_size, "sha256": sha256(p)}
                for p in package.iterdir()
                if p.name not in files
            }
        )
        report.update(
            status="complete",
            package=str(package),
            statistics=scene.stats,
            textures=textures,
            warnings=scene.warnings,
            dependencies=scene.dependencies,
            files=files,
        )
        write_json(out / "report.json", report)
        (out / "REPORT.md").write_text(
            f"# {args.title or args.map}\n\nConverted **{scene.engine}** into an IW8 1.20 Replay package. Offline checks passed; no game test.\n\n![Offline geometry preview](preview.png)\n\n"
            + "\n".join(f"- {key}: {value:,}" for key, value in scene.stats.items())
            + "\n\n"
            + "\n".join("- " + w for w in scene.warnings)
            + "\n\nSee report.json for input, dependency and output SHA-256 values.\n",
            encoding="utf-8",
        )
        print(
            json.dumps(
                {
                    "status": "complete",
                    "engine": scene.engine,
                    "package": str(package),
                    "statistics": scene.stats,
                    "report": str(out / "REPORT.md"),
                    "game_tested": False,
                },
                indent=2,
            )
        )
        return 0
    except (Exception, KeyboardInterrupt) as error:
        report.update(
            status="interrupted" if isinstance(error, KeyboardInterrupt) else "failed",
            error=f"{type(error).__name__}: {error}",
        )
        write_json(out / "report.json", report)
        raise


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv and argv[0] == "formats":
        print(
            json.dumps(
                {
                    "target": "IW8 1.20 Replay custom package",
                    "formats": FORMATS,
                    "unsupported": [
                        "raw ZoneTool/x64-zt gfxmap/clipmap blobs",
                        "IW6/S1/H1/H2/IW7/T7 fastfiles",
                        "Source 2 / Unreal packages",
                        "Source VBSP 21+",
                        "Quake BSP 29",
                        "arbitrary game logic and navigation",
                    ],
                },
                indent=2,
            )
        )
        return 0
    if argv and argv[0] == "import":
        argv = argv[1:]
    try:
        return run(parser().parse_args(argv))
    except (
        Exception,
        KeyboardInterrupt,
    ) as error:  # noqa: BLE001 - CLI boundary retains the failed-build report.
        print(f"Import failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
