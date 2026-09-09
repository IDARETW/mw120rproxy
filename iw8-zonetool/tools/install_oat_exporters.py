"""Install the IW4/IW5 map exporters into an existing OpenAssetTools source tree."""

import argparse
import shutil
from pathlib import Path


def install(root):
    base = root / "src/ObjWriting/Game"
    header = Path(__file__).parent / "oat/ReplayMapExport.h"
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
        backup = target.with_suffix(".cpp.pre-iw8-import")
        if not backup.exists():
            shutil.copy2(target, backup)
        target.write_text(text)
        shutil.copy2(header, base / engine / header.name)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("oat", type=Path)
    install(p.parse_args().oat.resolve())
