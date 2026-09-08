"""Install the IW4/IW5 map exporters into an existing OpenAssetTools source tree."""

import argparse
import shutil
import subprocess
from pathlib import Path

PIN = "7d027e8f89118196713e955b0e11f8404149c54d"


def install(root):
    root = root.resolve()
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    if revision != PIN:
        raise ValueError(f"Expected OpenAssetTools v0.33.0 {PIN}, found {revision}")
    base = root / "src/ObjWriting/Game"
    header = Path(__file__).parent / "oat/ReplayMapExport.h"
    updates = []
    for engine, assets in (
        ("IW4", ("AssetClipMapSp", "AssetClipMapMp")),
        ("IW5", ("AssetClipMap",)),
    ):
        target = base / engine / f"ObjWriter{engine}.cpp"
        text = target.read_text()
        if '#include "ReplayMapExport.h"' not in text:
            text = '#include "ReplayMapExport.h"\n' + text
        registrations = [
            f"    RegisterAssetDumper(std::make_unique<iw8_export::World<{engine}::AssetGfxWorld>>());"
        ]
        registrations += [
            f"    RegisterAssetDumper(std::make_unique<iw8_export::Collision<{engine}::{asset}>>());"
            for asset in assets
        ]
        marker = "void ObjWriter::RegisterAssetDumpers(AssetDumpingContext& context)\n{"
        if marker not in text:
            raise ValueError(f"Unsupported OAT source layout: {target}")
        if "iw8_export::World<" not in text:
            text = text.replace(marker, marker + "\n" + "\n".join(registrations), 1)
        updates.append((target, text))
    for target, text in updates:
        backup = target.with_suffix(".cpp.pre-iw8-import")
        if not backup.exists():
            shutil.copy2(target, backup)
        target.write_text(text)
        shutil.copy2(header, target.parent / header.name)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("oat", type=Path)
    install(p.parse_args().oat.resolve())
