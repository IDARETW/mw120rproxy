"""Validate a generated package with Atian's exact MW19 Replay loaders."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess


ZONE_PREFIXES = ("srv_", "", "eng_", "ww_", "techsets_")


def validate(acts: Path, game: Path, package: Path, map_id: str, out: Path, log: Path):
    acts = acts.resolve()
    game = game.resolve()
    package = package.resolve()
    out = out.resolve()
    log = log.resolve()
    if not acts.is_file():
        raise FileNotFoundError(f"Atian CLI was not found: {acts}")
    if not game.is_file():
        raise FileNotFoundError(f"Replay executable was not found: {game}")
    zones = [(package / f"{prefix}{map_id}.ff").resolve() for prefix in ZONE_PREFIXES]
    missing = [str(path) for path in zones if not path.is_file()]
    if missing:
        raise FileNotFoundError("Package zone family is incomplete: " + ", ".join(missing))

    if out.exists():
        if out.parent == out or package == out or package in out.parents:
            raise ValueError(f"Unsafe validation output path: {out}")
        shutil.rmtree(out)
    out.mkdir(parents=True)
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(acts),
        "--noUpdater",
        "fastfile",
        "-r",
        "mw19replay",
        "-g",
        str(game),
        "--noAssetDump",
        "--test",
        "--limit-per-pool",
        "32",
        "-o",
        str(out),
        *map(str, zones),
    ]
    with log.open("w", encoding="utf-8") as output:
        output.write(subprocess.list2cmdline(command) + "\n")
        output.flush()
        result = subprocess.run(command, cwd=package, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Atian validation failed ({result.returncode}); read {log}")

    manifests = {}
    for path in out.rglob("manifest.json"):
        data = json.loads(path.read_text(encoding="utf-8"))
        fastfile = Path(data.get("fastfile", "")).stem.lower()
        if fastfile:
            manifests[fastfile] = (path, data)

    expected = {path.stem.lower(): path for path in zones}
    if set(expected) - set(manifests):
        missing_names = ", ".join(sorted(set(expected) - set(manifests)))
        raise ValueError(f"Atian did not write manifests for: {missing_names}")

    report = {
        "profile": "replay-1.20",
        "map": map_id,
        "success": True,
        "zones": {},
    }
    for name in expected:
        manifest_path, data = manifests[name]
        if not data.get("complete") or not data.get("success"):
            raise ValueError(f"Atian loader did not complete {name}")
        if data.get("failed") != 0 or data.get("unavailable") != 0:
            raise ValueError(f"Atian exporter validation failed for {name}")
        report["zones"][name] = {
            "manifest": str(manifest_path),
            "loaded_assets": data.get("loaded_assets", 0),
            "tested": data.get("tested", 0),
            "failed": data.get("failed", 0),
            "unavailable": data.get("unavailable", 0),
            "serialized_bytes_read": data.get("serialized_bytes_read", 0),
            "loaded_by_type": data.get("loaded_by_type", {}),
        }

    server = report["zones"][f"srv_{map_id}"]["loaded_by_type"]
    main = report["zones"][map_id]["loaded_by_type"]
    required_server = {"map_ents": 2, "col_map": 1, "com_map": 1}
    required_main = {"gfx_map": 1, "gfx_map_trzone": 1, "glass_map": 1}
    for asset_type, count in required_server.items():
        if server.get(asset_type) != count:
            raise ValueError(f"Server zone has {server.get(asset_type, 0)} {asset_type}, expected {count}")
    for asset_type, count in required_main.items():
        if main.get(asset_type) != count:
            raise ValueError(f"Main zone has {main.get(asset_type, 0)} {asset_type}, expected {count}")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--acts", type=Path, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--map", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    validation = validate(
        args.acts,
        args.game,
        args.package,
        args.map,
        args.output_dir,
        args.log,
    )
    args.out.write_text(json.dumps(validation, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(validation, indent=2))
