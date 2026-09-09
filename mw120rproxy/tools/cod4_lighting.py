"""Run the supplied CoD4 light compiler and export its linked lightmaps offline."""

import json, shutil, os
from pathlib import Path


def compile_lighting(game, out, bsp, run, tools):
    # The supplied 64-bit light tool asserts in its allocator on this host.
    # The supplied x86 compiler completes the same map and model-shadow bake.
    run(
        [
            game / "bin/cod4rad.exe",
            "-platform",
            "pc",
            "-modelshadow",
            "-extra",
            bsp.with_suffix(""),
        ],
        game,
        out / "cod4rad.log",
    )
    if "ASSERTBEGIN" in (out / "cod4rad.log").read_text(errors="replace"):
        raise RuntimeError("CoD4 light compiler assertion")
    name = "mp_mw120r_light_" + str(os.getpid()) + "_" + out.name.replace("-", "")[-12:]
    raw = game / "raw/maps/mp" / (name + ".d3dbsp")
    csv = game / "zone_source" / (name + ".csv")
    ff = game / "zone/english" / (name + ".ff")
    if any(p.exists() for p in (raw, csv, ff)):
        raise RuntimeError("Lighting staging paths already exist")
    raw.parent.mkdir(parents=True, exist_ok=True)
    try:
        shutil.copy2(bsp, raw)
        csv.write_text("gfx_map,maps/mp/" + name + ".d3dbsp\n")
        run(
            [game / "bin/linker_pc.exe", "-nopause", "-language", "english", name],
            game / "bin",
            out / "light_link.log",
        )
        if "ERROR:" in (out / "light_link.log").read_text(errors="replace") or not ff.is_file():
            raise RuntimeError("CoD4 lighting fastfile failed; read light_link.log")
        linked = out / "lighting.ff"
        shutil.copy2(ff, linked)
        dump = out / "lighting"
        run(
            [
                tools / "_vendor/OpenAssetTools/build/bin/Release_x86/Unlinker.exe",
                "--no-color",
                "--skip-obj",
                "--include-assets",
                "gfxworld",
                "-o",
                dump,
                linked,
            ],
            out,
            out / "light_export.log",
        )
        return dump, json.loads((dump / f"maps/mp/{name}.d3dbsp.replay-world.json").read_text())
    finally:
        # Only exact, fresh staging files owned by this invocation are removed.
        for p in (raw, csv, ff):
            if p.exists():
                p.unlink()
