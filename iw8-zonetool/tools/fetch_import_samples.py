"""Download pinned public upstream fixtures without extracting or executing them."""

import argparse
import hashlib
import json
from pathlib import Path
from urllib.parse import quote
from urllib.request import Request, urlopen

FIXTURES = [
    (
        "q2_lobby.bsp",
        "snake-biscuits/bsp_tool",
        "9781c5286113bf82d21488c7611fe4041dd45b2f",
        "tests/maps/Quake 2/mp_lobby.bsp",
    ),
    (
        "q3_lobby.bsp",
        "snake-biscuits/bsp_tool",
        "9781c5286113bf82d21488c7611fe4041dd45b2f",
        "tests/maps/Quake 3 Arena/mp_lobby.bsp",
    ),
    (
        "source_lobby.bsp",
        "snake-biscuits/bsp_tool",
        "9781c5286113bf82d21488c7611fe4041dd45b2f",
        "tests/maps/Team Fortress 2/mp_lobby.bsp",
    ),
    (
        "source_displacement.bsp",
        "snake-biscuits/bsp_tool",
        "9781c5286113bf82d21488c7611fe4041dd45b2f",
        "tests/maps/Team Fortress 2/test_displacement_decompile.bsp",
    ),
    (
        "box_textured.glb",
        "KhronosGroup/glTF-Sample-Assets",
        "9429648735279342b4c32b8745f7904196607379",
        "Models/BoxTextured/glTF-Binary/BoxTextured.glb",
    ),
    (
        "bsp_tool_LICENSE.txt",
        "snake-biscuits/bsp_tool",
        "9781c5286113bf82d21488c7611fe4041dd45b2f",
        "LICENSE.txt",
    ),
    (
        "BoxTextured_README.md",
        "KhronosGroup/glTF-Sample-Assets",
        "9429648735279342b4c32b8745f7904196607379",
        "Models/BoxTextured/README.md",
    ),
    (
        "assimp_LICENSE.txt",
        "assimp/assimp",
        "630f614a5663405c00c1fb5395f1e43435402941",
        "LICENSE",
    ),
]

for name in (
    "spider.obj",
    "spider.mtl",
    "SpiderTex.jpg",
    "wal67ar_small.jpg",
    "wal69ar_small.jpg",
    "drkwood2.jpg",
    "engineflare1.jpg",
):
    FIXTURES.append(
        (
            name,
            "assimp/assimp",
            "630f614a5663405c00c1fb5395f1e43435402941",
            "test/models/OBJ/" + name,
        )
    )

EXPECTED = {
    "assimp_LICENSE.txt": "21195d410708b82757d3e34c3a40c427f6e59339da862e9480d4fcdc79b7a1bd",
    "q2_lobby.bsp": "b63739a560793dc29947a7f92b9254a53d943342eea019f8dc26e8c03f924cc4",
    "q3_lobby.bsp": "8a059bb033bfcdb778bc4b52c29a6999264e946ddf830992fd50ace9802e9054",
    "source_lobby.bsp": "5baa19716fc06c66a814afe9d87d3fd8ec684d6b457c43afbeb3d74d223995a1",
    "source_displacement.bsp": "2032b092479f83ce1705fd5cfb90a63d19df368859e3583331d3032d53eb16d2",
    "box_textured.glb": "b510eca2e2ef33f62f9ed57d6e7ce2d10ebb2bdebc4a8e59d347719ba81abdf4",
    "bsp_tool_LICENSE.txt": "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986",
    "BoxTextured_README.md": "5ccb8002776081436039f9538dd9eaa9d9faa7843e62efcfaf6ff60eee0f07cc",
    "spider.obj": "a176f0223a6e74e90185c067ed45f928257e775cad7e17687ed4612a3343c206",
    "spider.mtl": "64f270152d0f7d70cd635e8f074f73f0d62f0432e05ead7a0a8a80290a61f1d3",
    "SpiderTex.jpg": "21d4dc5134073a0fbcf77dc624c7ccf294cdbe43f9c24db006faabff0097b2ca",
    "wal67ar_small.jpg": "cf5be18b5b620bfc9f5fe437aeae5ee982dafdf289ce23c5d4414e8ded283d39",
    "wal69ar_small.jpg": "64def7334567c818ef738c666fe2e8729756a6ad07d09a9aabb198ca26260a1b",
    "drkwood2.jpg": "d6cd16534d2bf5b9dea08d8daa33233e72b11508ee99779681cd18895bf37424",
    "engineflare1.jpg": "f67bed6b7c8f27a34b013b4deaf97a6d162e8eda1927581b9fcf3d1223a146d7",
}


def fetch(out):
    out.mkdir(parents=True, exist_ok=True)
    records = []
    for local, repo, commit, path in FIXTURES:
        url = f"https://raw.githubusercontent.com/{repo}/{commit}/" + quote(
            path, safe="/"
        )
        target = out / local
        if not target.exists():
            with urlopen(
                Request(url, headers={"User-Agent": "iw8-zonetool-offline-tests"}),
                timeout=45,
            ) as response:
                data = response.read(8 * 1024 * 1024 + 1)
            if len(data) > 8 * 1024 * 1024:
                raise ValueError("Fixture exceeds download limit")
            target.write_bytes(data)
        data = target.read_bytes()
        if hashlib.sha256(data).hexdigest() != EXPECTED[local]:
            raise ValueError(f"Pinned fixture hash mismatch: {target}")
        records.append(
            {
                "file": local,
                "repository": repo,
                "commit": commit,
                "path": path,
                "url": url,
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    (out / "provenance.json").write_text(json.dumps(records, indent=2) + "\n")
    print(json.dumps(records, indent=2))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    fetch(p.parse_args().output)
