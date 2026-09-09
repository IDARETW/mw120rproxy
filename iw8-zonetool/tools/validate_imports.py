"""Reproduce offline conversions and optional read-only Replay layout checks."""

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

from mapimport.core import sha256, write_json

ROOT = Path(__file__).resolve().parents[1]


def validate(args):
    out = args.output.resolve()
    if out.exists():
        raise ValueError("Use a fresh validation directory")
    out.mkdir(parents=True)
    samples = args.samples.resolve()
    writer = ROOT / "xmake-out/x64/Release/iw8-zonetool.exe"
    # The IW4/IW5 fixtures exercise the production exporters against actual OAT types.
    native = ROOT / "tests/bin/oat-export-fixture.exe"
    with (out / "native-fixture.log").open("w") as log:
        subprocess.run(
            [str(native), str(out / "oat_fixtures")],
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=60,
        )
    # A curved v47 fixture is controlled evidence, not a downloaded Quake Live map.
    sys.path.insert(0, str(ROOT / "tests"))
    from test_import import q3_fixture

    ql = bytearray(q3_fixture(patch=True))
    struct.pack_into("<i", ql, 4, 47)
    (out / "ql_patch_fixture.bsp").write_bytes(ql)
    cases = [
        ("q2", samples / "q2_lobby.bsp", [], "downloaded BSP"),
        ("q3", samples / "q3_lobby.bsp", [], "downloaded BSP"),
        ("source", samples / "source_lobby.bsp", [], "downloaded BSP"),
        (
            "terrain",
            samples / "source_displacement.bsp",
            ["--spawn", "0", "0", "128"],
            "downloaded Source displacement BSP",
        ),
        (
            "obj",
            samples / "spider.obj",
            ["--spawn", "0", "0", "128", "--strict-textures"],
            "downloaded OBJ/MTL and images",
        ),
        (
            "gltf",
            samples / "box_textured.glb",
            ["--spawn", "0", "0", "72", "--strict-textures"],
            "downloaded GLB with embedded image",
        ),
        (
            "ql",
            out / "ql_patch_fixture.bsp",
            ["--graybox"],
            "controlled quadratic patch fixture with v47 header",
        ),
        (
            "iw4",
            out / "oat_fixtures/iw4",
            ["--format", "iw4", "--graybox"],
            "native OAT IW4 production exporter fixture",
        ),
        (
            "iw5",
            out / "oat_fixtures/iw5",
            ["--format", "iw5", "--graybox"],
            "native OAT IW5 production exporter fixture",
        ),
    ]
    records = []
    for label, source, extra, evidence in cases:
        mapid = "mp_import_" + label
        build = out / label
        command = [
            str(writer),
            "import",
            str(source),
            mapid,
            "-o",
            str(build),
            "--title",
            "Offline import: " + label,
            "--credit",
            evidence,
        ] + extra
        with (out / (label + ".log")).open("w") as log:
            subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300
            )
        report = json.loads((build / "report.json").read_text())
        with (build / "native-collision.log").open("w") as log:
            subprocess.run(
                [
                    str(ROOT / "tests/bin/collision-validate.exe"),
                    str(build / "package/collision.bin"),
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=60,
            )
        record = {
            "case": label,
            "evidence": evidence,
            "report": str(build / "report.json"),
            "statistics": report["statistics"],
            "package": report["package"],
            "game_tested": False,
            "native_collision_parser": "passed",
        }
        if args.replay_exe:
            script = args.replay_tools / "verify_replay_map_layout.py"
            with (build / "layout.log").open("w") as log:
                subprocess.run(
                    [
                        sys.executable,
                        str(script),
                        "--game",
                        str(args.replay_exe),
                        "--package",
                        report["package"],
                        "--map",
                        mapid,
                        "--out",
                        str(build / "layout.json"),
                    ],
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=True,
                    timeout=120,
                )
            record["exact_replay_layouts"] = 5
        records.append(record)
        write_json(
            out / "validation.json",
            {"status": "running", "cases": records, "game_tested": False},
        )
        print(label + ": converted and validated (" + evidence + ")", flush=True)
    write_json(
        out / "validation.json",
        {
            "status": "complete",
            "cases": records,
            "writer_sha256": sha256(writer),
            "replay_exe_sha256": sha256(args.replay_exe) if args.replay_exe else None,
            "game_tested": False,
        },
    )
    return records


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument(
        "--samples",
        type=Path,
        default=ROOT / "evidence/multi_engine_20260908/downloads",
    )
    p.add_argument(
        "--replay-exe",
        type=Path,
        help="Read PE layout constants from this file; never launch it",
    )
    p.add_argument(
        "--replay-tools",
        type=Path,
        default=ROOT.parent / "mw120rproxy/tools",
    )
    args = p.parse_args()
    existed = args.output.exists()
    try:
        validate(args)
    except (Exception, KeyboardInterrupt) as error:
        if not existed and args.output.is_dir():
            result = args.output / "validation.json"
            previous = (
                json.loads(result.read_text()) if result.exists() else {"cases": []}
            )
            previous.update(
                status="interrupted"
                if isinstance(error, KeyboardInterrupt)
                else "failed",
                error=str(error),
                game_tested=False,
            )
            write_json(result, previous)
        raise
