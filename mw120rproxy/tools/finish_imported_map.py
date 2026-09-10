"""Finalize or resume an already converted map without rebuilding its geometry."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from build_mp_test import REPO, TOOLS, REPLAY, run, write_json, numeric_entities
from radiant_source import blocks, properties
import radiant_collision


def finish(out, mapid, title, credit, source, dump=None):
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", mapid):
        raise ValueError("Invalid map id")
    out = out.resolve()
    source = source.resolve()
    dump = (dump or out / "dump").resolve()
    package = out / "package"
    manifest = json.loads((package / "manifest.json").read_text())
    if manifest["id"] != mapid:
        raise ValueError("Package map mismatch")
    brushes = json.loads((out / "source_collision.json").read_text())
    data = radiant_collision.encode(brushes)
    radiant_collision.validate(data)
    (package / "collision.bin").write_bytes(data)
    manifest.update(
        title=title,
        description="Imported CoD4 map: static world and props, TDM. " + credit,
        collision="convex-v3" if data[:8] == b"MWCOLL03" else "convex-v2",
        visibility="all-visible-v1",
        world="replay-1.20-native-v1",
    )
    if (out / "ladders.bin").exists():
        import ladder_data

        ladder_bytes = (out / "ladders.bin").read_bytes()
        ladder_data.validate(ladder_bytes)
        (package / "ladders.bin").write_bytes(ladder_bytes)
        manifest["ladders"] = "faces-v2" if ladder_bytes[:8] == b"MWRLAD02" else "faces-v1"
    if (out / "glass.bin").exists():
        from glass_panes import validate

        data = (out / "glass.bin").read_bytes()
        validate(data)
        (package / "glass.bin").write_bytes(data)
        manifest["glass"] = "panes-v2" if data[:8] == b"MWRGLS02" else "panes-v1"
    if (out / "doors.bin").exists():
        (package / "doors.bin").write_bytes((out / "doors.bin").read_bytes())
        manifest["doors"] = "brush-poses-v1"
    write_json(package / "manifest.json", manifest)
    from map_presentation import preview

    preview(package, mapid, source)
    converter = REPO / "iw8-zonetool/xmake-out/x64/Release/iw8-zonetool.exe"
    run([converter, "validate-output", package, mapid], REPO, out / "validate.log")
    run(
        [
            sys.executable,
            TOOLS / "verify_replay_map_layout.py",
            "--game",
            REPLAY,
            "--package",
            package,
            "--map",
            mapid,
            "--out",
            out / "layout.json",
        ],
        REPO,
        out / "layout.log",
    )
    acts = REPO / "external/atian-cod-tools/build/bin/Release/acts.exe"
    acts_report = out / "acts-validation.json"
    if acts.is_file():
        run(
            [
                sys.executable,
                TOOLS / "validate_replay_package_acts.py",
                "--acts",
                acts,
                "--game",
                REPLAY,
                "--package",
                package,
                "--map",
                mapid,
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
    if (out / "import_report.json").exists():
        report = json.loads((out / "import_report.json").read_text())
    else:
        # Recovery for early builds that predate the pre-packaging checkpoint.
        mesh = json.loads((dump / f"maps/mp/{mapid}.d3dbsp.render.json").read_text())
        report = {
            "surfaces": len(mesh["surfaces"]),
            "vertices": sum(len(s["vertices"]) for s in mesh["surfaces"]),
            "triangles": sum(len(s["indices"]) // 3 for s in mesh["surfaces"]),
            "recovered_from_intermediates": True,
        }
        del mesh
        world = json.loads((source / f"maps/mp/{mapid}.d3dbsp.replay-world.json").read_text())
        text = (source / f"maps/mp/{mapid}.d3dbsp.ents").read_text()
        _, spawns, omitted = numeric_entities([properties(b) for b in blocks(text)])
        report.update(
            models=len(world["models"]), sky=world["sky"], spawns=spawns, omitted_entities=omitted
        )
    if "color_images" in report and "materials" in report:
        from footstep_data import generate

        mesh = json.loads((dump / f"maps/mp/{mapid}.d3dbsp.render.json").read_text())
        report["footsteps"] = generate(package, source, report, mesh)
        manifest = json.loads((package / "manifest.json").read_text())
        manifest["footsteps"] = "triangles-v1"
        write_json(package / "manifest.json", manifest)
    report.update(
        map=mapid,
        title=title,
        credit=credit,
        source=str(source),
        package=str(package),
        collision_hulls=len(brushes),
        game_tested=False,
        files={
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in package.iterdir()
            if p.is_file()
        },
    )
    if acts_report.is_file():
        report["native_loader_validation"] = json.loads(acts_report.read_text())
    else:
        report["native_loader_validation"] = {
            "success": False,
            "skipped": True,
            "reason": "AtianCodToolsCLI was not available",
        }
    report["limitations"] = [
        "CoD4 scripts and general dynamic destruction are not executed.",
        "Collision uses compiled brushes and collision triangles; embedded prop physics is not imported.",
        "Planar windows use imported pane collision and native Replay shatter effects/audio on shot, melee and mantle breaks. World lighting combines source indirect lightmaps with Replay sun visibility. Native GPU light grids are not yet generated.",
        "No bot navigation data is generated.",
    ]
    write_json(out / "build_report.json", report)
    print(
        json.dumps(
            {
                k: report[k]
                for k in ("map", "package", "surfaces", "triangles", "collision_hulls", "models")
            },
            indent=2,
        ),
        flush=True,
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--map", required=True)
    p.add_argument("--title", required=True)
    p.add_argument("--credit", default="")
    p.add_argument("--dump", type=Path)
    a = p.parse_args()
    finish(a.build, a.map, a.title, a.credit, a.source, a.dump)
