"""Decode CoD4 images and prepare the basic mp_test texture input.

Reads only the three named IWI files from loose images or local IWD archives.
Writes a separate candidate input directory; never edits the playable v12 dump,
builds, deploys, or starts Replay. The output still needs native layout checks
and an in-game visual test. Requires Pillow; wavelet images also require the
OpenAssetTools ImageConverter built alongside Unlinker.
"""

import argparse
import hashlib
import io
import json
from local_paths import UNLINKER
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[2] / "custom_map_sources/mp_test"
NAMES = ("ch_tile_floor03_col", "ch_tile_floor05_col", "chechnya_ft")


def decode_wavelet(data):
    """Use OpenAssetTools' IW Huffman/wavelet decoder, retaining every image face."""
    converter = UNLINKER.with_name("ImageConverter.exe")
    if not converter.is_file():
        raise FileNotFoundError(
            "Wavelet IWI decoding requires the OpenAssetTools ImageConverter executable: "
            + str(converter)
        )
    with tempfile.TemporaryDirectory(prefix="mw120r-iwi-") as temporary:
        source = Path(temporary) / "image.iwi"
        source.write_bytes(data)
        decoded = source.with_suffix(".dds")
        # DDS preserves all mip levels and cubemap faces. Writing it back as a
        # bitmap IWI reuses the established face/mip parser below.
        for command, output in (
            ([converter, "--no-color", source], decoded),
            ([converter, "--no-color", "--iw3", decoded], source),
        ):
            result = subprocess.run(
                [str(part) for part in command],
                capture_output=True,
                text=True,
                timeout=60,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            if result.returncode or not output.is_file():
                raise ValueError(
                    "OpenAssetTools could not decode wavelet IWI: "
                    + (result.stdout + result.stderr)[-2000:]
                )
        bitmap = source.read_bytes()
        if len(bitmap) < 28 or bitmap[4] in range(6, 11):
            raise ValueError("Wavelet decoder did not produce a bitmap IWI")
        if bitmap[6:10] != data[6:10] or (bitmap[5] & 12) != (data[5] & 12):
            raise ValueError("Wavelet decoder changed image dimensions or face layout")
        return decode_iwi(bitmap)


def decode_iwi(data):
    # CoD4 v6 header and cube flag are documented by mjkzy/x64-zt utils/iwi.
    # Mips run smallest to largest. The last mip contains all six cube faces.
    if len(data) < 28 or data[:4] != b"IWi\x06":
        raise ValueError("Expected a CoD4 v6 IWI image")
    fmt, flags = data[4:6]
    width, height, depth = struct.unpack_from("<3H", data, 6)
    file_sizes = struct.unpack_from("<4I", data, 12)
    if fmt not in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13) or flags & 8 or depth not in (0, 1):
        raise ValueError(f"Unsupported 2D/cube IWI format {fmt}")
    if not 1 <= width <= 4096 or not 1 <= height <= 4096:
        raise ValueError("Invalid IWI dimensions")
    faces = 6 if flags & 4 else 1
    if faces == 6 and width != height:
        raise ValueError("Cubemap faces must be square")
    if 6 <= fmt <= 10:
        if file_sizes[0] != len(data):
            raise ValueError("Truncated wavelet IWI or inconsistent image size")
        return decode_wavelet(data)
    block_bytes = 8 if fmt == 11 else 16
    size = (
        width * height * {1: 4, 2: 3, 3: 2, 4: 1, 5: 1}[fmt]
        if fmt <= 5
        else ((width + 3) // 4) * ((height + 3) // 4) * block_bytes
    )
    start = len(data) - size * faces
    if file_sizes[0] != len(data) or start < 28 or (flags & 2 and start != 28):
        raise ValueError("Truncated IWI or inconsistent image size")
    if fmt <= 5:
        result = []
        for face in range(faces):
            payload = data[start + face * size : start + (face + 1) * size]
            if fmt in (1, 2):
                mode, rawmode = ("RGBA", "BGRA") if fmt == 1 else ("RGB", "BGR")
                image = Image.frombytes(mode, (width, height), payload, "raw", rawmode).convert(
                    "RGBA"
                )
            elif fmt == 5:
                image = Image.new("RGBA", (width, height), (255, 255, 255, 255))
                image.putalpha(Image.frombytes("L", (width, height), payload))
            else:
                image = Image.frombytes(
                    "LA" if fmt == 3 else "L", (width, height), payload
                ).convert("RGBA")
            result.append(image)
        return result
    # Wrap each top-level compressed face in a DDS header for Pillow's BC decoder.
    header = [124, 0x81007, height, width, size, 0, 1] + [0] * 11
    fourcc = (b"DXT1", b"DXT3", b"DXT5")[fmt - 11]
    header += [32, 4, int.from_bytes(fourcc, "little"), 0, 0, 0, 0, 0]
    header += [0x1000, 0, 0, 0, 0]
    result = []
    for face in range(faces):
        payload = data[start + face * size : start + (face + 1) * size]
        with Image.open(io.BytesIO(b"DDS " + struct.pack("<31I", *header) + payload)) as image:
            result.append(image.convert("RGBA"))
    return result


def find_images(source):
    wanted = {n + ".iwi" for n in NAMES}
    found = {}
    for directory in (source, source / "images", source / "raw/images", source / "main/images"):
        for name in sorted(wanted - found.keys()):
            path = directory / name
            if path.is_file():
                if path.stat().st_size > 64 * 1024 * 1024:
                    raise ValueError("IWI exceeds input limit")
                found[name] = (path.read_bytes(), str(path))
    archives = (
        [source]
        if source.is_file() and source.suffix.lower() == ".iwd"
        else sorted(source.rglob("*.iwd"), reverse=True)
    )
    for archive in archives:
        if len(found) == len(wanted):
            break
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                entry = info.filename.replace("\\", "/").lower()
                name = entry.removeprefix("images/")
                if entry != "images/" + name or name not in wanted or name in found:
                    continue
                if info.file_size > 64 * 1024 * 1024:
                    raise ValueError("Archived IWI exceeds input limit")
                found[name] = (z.read(info), str(archive) + "!" + info.filename)
    missing = sorted(wanted - found.keys())
    if missing:
        raise FileNotFoundError("Missing original image files: " + ", ".join(missing))
    return found


def sky_surfaces():
    # Direct3D cubemap ordering +X,-X,+Y,-Y,+Z,-Z. The sky is a distant
    # enclosure with no physics, separate from the source's solid BSP surfaces.
    mappings = (
        lambda u, v: (1, -v, -u),
        lambda u, v: (-1, -v, u),
        lambda u, v: (u, 1, v),
        lambda u, v: (u, -1, -v),
        lambda u, v: (u, -v, 1),
        lambda u, v: (-u, -v, -1),
    )
    result = []
    for face, mapping in enumerate(mappings):
        vertices = [
            {
                "position": [float(x * 32768) for x in mapping(u * 2 - 1, v * 2 - 1)],
                "uv": [u, v],
                "normal": 3489135616,
                "lightmapUV": [face + 2, 0],
            }
            for u, v in ((0, 0), (1, 0), (1, 1), (0, 1))
        ]
        a, b, c = [v["position"] for v in vertices[:3]]
        ab = [b[k] - a[k] for k in range(3)]
        ac = [c[k] - a[k] for k in range(3)]
        cross = [
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        ]
        indices = [0, 1, 2, 2, 3, 0]
        # Replay's retained BSP pipeline renders the clockwise side: original
        # CoD4 solid triangles have cross(edge1,edge2) dot normal < 0. For a
        # visible interior sky, the geometric cross must therefore point OUT.
        if sum(cross[k] * a[k] for k in range(3)) < 0:
            indices = [0, 2, 1, 2, 0, 3]
        result.append({"vertices": vertices, "indices": indices})
    return result


def prepare(source, target):
    found = find_images(source)
    decoded = [decode_iwi(found[n + ".iwi"][0]) for n in NAMES]
    if [len(x) for x in decoded] != [1, 1, 6]:
        raise ValueError("Expected two 2D tile images and one six-face sky cubemap")
    if target.exists() and any(target.iterdir()):
        raise ValueError("Output must be a new or empty directory; preserve earlier candidates")
    cell = max(16, *(max(im.size) for group in decoded for im in group))
    if cell > 1024:
        raise ValueError("Atlas source exceeds the supported 1024-pixel cell size")
    atlas = Image.new("RGBA", (cell * 4, cell * 2))
    for i, image in enumerate([im for group in decoded for im in group]):
        atlas.paste(
            image.resize((cell, cell), Image.Resampling.LANCZOS), ((i % 4) * cell, (i // 4) * cell)
        )
    shutil.copytree(ROOT / "dump", target / "dump")
    folder = target / "dump/maps/mp"
    pixels = atlas.tobytes()
    (folder / "mp_test_color_atlas.rgba").write_bytes(pixels)
    atlas.save(target / "texture_atlas_preview.png")
    material_path = folder / "mp_test.d3dbsp.material.json"
    material = json.loads(material_path.read_text())
    image_name = "mw120r/mp_test_color_" + hashlib.sha256(pixels).hexdigest()[:16]
    material["textures"][0]["image"] = image_name
    material["imageDefinitions"] = [
        {
            "name": image_name,
            "width": atlas.width,
            "height": atlas.height,
            "rgba8": "mp_test_color_atlas.rgba",
        }
    ]
    material_path.write_text(json.dumps(material, indent=2) + "\n")
    mesh_path = folder / "mp_test.d3dbsp.render.json"
    mesh = json.loads(mesh_path.read_text())
    geometry = json.loads((ROOT / "geometry.json").read_text())["simple"]
    source_surfaces = [
        s
        for s in geometry["surfaces"]
        if int.from_bytes(bytes.fromhex(s["source_metadata_hex"])[:2], "little") != 2
    ]
    if len(mesh["surfaces"]) != len(source_surfaces):
        raise ValueError("Source and prepared solid surface counts differ")
    for surface, original in zip(mesh["surfaces"], source_surfaces):
        tile = int.from_bytes(bytes.fromhex(original["source_metadata_hex"])[:2], "little")
        if tile not in (0, 1):
            raise ValueError("Unexpected source material")
        ids = list(dict.fromkeys(i for tri in original["triangles"] for i in tri))
        expected = [geometry["vertices"][i] for i in ids]
        if len(expected) != len(surface["vertices"]) or any(
            v["position"] != e["position"] or v["uv"] != e["uv"]
            for v, e in zip(surface["vertices"], expected)
        ):
            raise ValueError("Prepared solid geometry/UVs differ from the original BSP")
        for vertex in surface["vertices"]:
            vertex["lightmapUV"] = [tile, 0]
    mesh["surfaces"] += sky_surfaces()
    mesh_path.write_text(json.dumps(mesh, indent=2) + "\n")
    tools = Path(__file__).resolve().parent
    subprocess.run(
        [
            sys.executable,
            str(tools / "compile_graybox_shader.py"),
            "--source",
            str(tools / "mp_test_textured.hlsl"),
            "--target-root",
            str(target),
        ],
        check=True,
    )
    report = {
        "schema": 1,
        "source_images": {
            n: {"path": p, "sha256": hashlib.sha256(d).hexdigest()} for n, (d, p) in found.items()
        },
        "atlas_size": list(atlas.size),
        "atlas_sha256": hashlib.sha256(pixels).hexdigest(),
        "solid_surfaces": len(source_surfaces),
        "sky_surfaces": 6,
        "game_tested": False,
        "limitations": [
            "Color textures only; original normal/specular maps and baked lighting are not imported.",
            "Distant sky enclosure; cube orientation and native culling require in-game visual verification.",
        ],
    }
    (target / "texture_import.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cod4",
        type=Path,
        required=True,
        help="CoD4 installation, loose image directory, or one IWD",
    )
    parser.add_argument("--out", type=Path, required=True, help="Separate prepared input directory")
    args = parser.parse_args()
    print(json.dumps(prepare(args.cod4.resolve(), args.out.resolve()), indent=2))
