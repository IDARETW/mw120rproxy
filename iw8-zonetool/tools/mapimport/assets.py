"""Bounded asset lookup: loose directories, ZIP/PK3/IWD and Quake PAK."""

import io
import re
import struct
import zipfile
from pathlib import Path

from PIL import Image

from .core import read_bytes, safe_path, sha256


class Assets:
    def __init__(self, roots, scene):
        self.scene = scene
        self.roots = []
        self.archives = []
        for root in roots:
            root = Path(root).resolve()
            if root.is_dir():
                self.roots.append(root)
            elif root.suffix.lower() in (".zip", ".pk3", ".iwd"):
                self.add_zip(read_bytes(root), str(root))
                scene.dependencies[str(root)] = sha256(root)
            elif root.suffix.lower() == ".pak":
                raw = read_bytes(root)
                if len(raw) < 12 or raw[:4] != b"PACK":
                    raise ValueError("Invalid Quake PAK")
                offset, size = struct.unpack_from("<ii", raw, 4)
                if offset < 12 or size < 0 or size % 64 or offset + size > len(raw):
                    raise ValueError("Invalid Quake PAK directory")
                entries = {}
                for name, start, length in struct.iter_unpack(
                    "<56sii", raw[offset : offset + size]
                ):
                    key = name.split(b"\0", 1)[0].decode("utf-8").lower()
                    safe_path(root.parent, key)
                    if (
                        key in entries
                        or min(start, length) < 0
                        or start + length > len(raw)
                    ):
                        raise ValueError("Invalid or duplicate Quake PAK entry")
                    entries[key] = raw[start : start + length]
                self.archives.append((str(root), entries))
                scene.dependencies[str(root)] = sha256(root)
            else:
                raise ValueError(f"Unknown asset search root: {root}")
        if hasattr(scene, "_pak"):
            self.add_zip(scene._pak, "Source BSP embedded pak")

    def add_zip(self, raw, label):
        z = zipfile.ZipFile(io.BytesIO(raw))
        entries = {}
        if len(z.infolist()) > 100000:
            raise ValueError("Archive file count limit")
        for entry in z.infolist():
            if entry.is_dir():
                continue
            key = entry.filename.replace("\\", "/").lower()
            safe_path(Path.cwd(), key)
            if key in entries or entry.file_size > 64 * 1024 * 1024:
                raise ValueError("Duplicate or oversized archive entry")
            entries[key] = entry
        self.archives.append((label, (z, entries)))

    def read(self, name):
        for root in self.roots:
            path = safe_path(root, name)
            if path.is_file():
                self.scene.dependencies[str(path)] = sha256(path)
                return read_bytes(path)
        for label, archive in self.archives:
            key = name.replace("\\", "/").lower()
            safe_path(Path.cwd(), key)
            if isinstance(archive, tuple):
                z, entries = archive
                if key in entries:
                    return z.read(entries[key])
            elif key in archive:
                return archive[key]
        return None

    def names(self):
        """Archive member inventory without extracting any files."""
        result = []
        for _, archive in self.archives:
            entries = archive[1] if isinstance(archive, tuple) else archive
            result.extend(entries)
        return sorted(set(result))

    def image(self, material):
        if "image_bytes" in material:
            return decode(material["image_bytes"])
        if "texture_path" in material:
            path = Path(material["texture_path"])
            self.scene.dependencies[str(path)] = sha256(path)
            return decode(read_bytes(path))
        name = material.get("texture")
        if not name:
            return None
        if material.get("source_vmt"):
            vmt = self.read(name + ".vmt")
            if vmt:
                match = re.search(
                    r'"?\$basetexture"?\s+"?([^"\s{}]+)',
                    vmt.decode("utf-8-sig"),
                    re.IGNORECASE,
                )
                if match:
                    name = "materials/" + match.group(1)
        for ext in (
            "",
            ".png",
            ".tga",
            ".jpg",
            ".jpeg",
            ".dds",
            ".iwi",
            ".vtf",
            ".wal",
        ):
            raw = self.read(name + ext)
            if raw is None:
                continue
            if ext == ".wal":
                if len(raw) < 100:
                    raise ValueError("Truncated WAL texture")
                w, h, offset = struct.unpack_from("<III", raw, 32)
                if (
                    not 1 <= w <= 4096
                    or not 1 <= h <= 4096
                    or offset + w * h > len(raw)
                ):
                    raise ValueError("Invalid WAL texture")
                palette = self.read("pics/colormap.pcx")
                if palette is None:
                    return None
                with Image.open(io.BytesIO(palette)) as pal:
                    im = Image.frombytes("P", (w, h), raw[offset : offset + w * h])
                    im.putpalette(pal.getpalette())
                    return im.convert("RGBA")
            return decode(raw)
        return None


def dds(payload, w, h, fourcc):
    header = [124, 0x81007, h, w, len(payload), 0, 1] + [0] * 11
    header += [32, 4, int.from_bytes(fourcc, "little"), 0, 0, 0, 0, 0] + [
        0x1000,
        0,
        0,
        0,
        0,
    ]
    with Image.open(io.BytesIO(b"DDS " + struct.pack("<31I", *header) + payload)) as im:
        return im.convert("RGBA")


def decode(raw):
    if raw[:3] == b"IWi":
        if len(raw) < 28 or raw[3] != 6:
            raise ValueError(
                "Only IWI v6 is supported directly; export other CoD image versions as DDS"
            )
        fmt, flags = raw[4:6]
        w, h, depth = struct.unpack_from("<3H", raw, 6)
        if (
            flags & 12
            or depth not in (0, 1)
            or not 1 <= w <= 4096
            or not 1 <= h <= 4096
        ):
            raise ValueError("Unsupported IWI cube/volume/dimensions")
        if struct.unpack_from("<I", raw, 12)[0] != len(raw):
            raise ValueError("Invalid IWI file size")
        if fmt in (11, 12, 13):
            size = ((w + 3) // 4) * ((h + 3) // 4) * (8 if fmt == 11 else 16)
            if len(raw) - size < 28:
                raise ValueError("Truncated IWI image")
            return dds(raw[-size:], w, h, {11: b"DXT1", 12: b"DXT3", 13: b"DXT5"}[fmt])
        modes = {
            1: ("RGBA", "BGRA", 4),
            2: ("RGB", "BGR", 3),
            3: ("LA", "LA", 2),
            4: ("L", "L", 1),
            5: ("L", "L", 1),
        }
        if fmt not in modes:
            raise ValueError("Unsupported IWI pixel format")
        mode, rawmode, bpp = modes[fmt]
        size = w * h * bpp
        if len(raw) - size < 28:
            raise ValueError("Truncated IWI pixels")
        im = Image.frombytes(mode, (w, h), raw[-size:], "raw", rawmode)
        if fmt == 5:
            result = Image.new("RGBA", (w, h), "white")
            result.putalpha(im)
            return result
        return im.convert("RGBA")
    if raw[:4] == b"VTF\0":
        if len(raw) < 80:
            raise ValueError("Truncated VTF")
        major, minor, header = struct.unpack_from("<III", raw, 4)
        if major != 7 or minor > 2:
            raise ValueError(
                "VTF resource-directory versions require DDS export; direct support is 7.0..7.2"
            )
        w, h = struct.unpack_from("<HH", raw, 16)
        flags = struct.unpack_from("<I", raw, 20)[0]
        frames = struct.unpack_from("<H", raw, 24)[0]
        fmt = struct.unpack_from("<I", raw, 52)[0]
        mips = raw[56]
        lowfmt = struct.unpack_from("<I", raw, 57)[0]
        lw, lh = raw[61:63]
        depth = struct.unpack_from("<H", raw, 63)[0] if minor >= 2 else 1
        if (
            frames != 1
            or depth != 1
            or flags & 0x4000
            or not 1 <= w <= 4096
            or not 1 <= h <= 4096
            or not 1 <= mips <= 13
        ):
            raise ValueError("Unsupported VTF animation/cubemap/dimensions")

        def size(w, h):
            if fmt in (13, 14, 15):
                return ((w + 3) // 4) * ((h + 3) // 4) * (8 if fmt == 13 else 16)
            bpp = {0: 4, 2: 3, 3: 3, 12: 4}.get(fmt)
            if not bpp:
                raise ValueError(f"Unsupported VTF image format {fmt}")
            return w * h * bpp

        if lw and lh and lowfmt != 13:
            raise ValueError("Unsupported VTF thumbnail format")
        offset = header + (((lw + 3) // 4) * ((lh + 3) // 4) * 8 if lw and lh else 0)
        offset += sum(
            size(max(1, w >> level), max(1, h >> level)) for level in range(1, mips)
        )
        payload = raw[offset : offset + size(w, h)]
        if len(payload) != size(w, h):
            raise ValueError("Truncated VTF top mip")
        if fmt in (13, 14, 15):
            return dds(payload, w, h, {13: b"DXT1", 14: b"DXT3", 15: b"DXT5"}[fmt])
        return Image.frombytes(
            "RGBA" if fmt in (0, 12) else "RGB",
            (w, h),
            payload,
            "raw",
            {0: "RGBA", 2: "RGB", 3: "BGR", 12: "BGRA"}[fmt],
        ).convert("RGBA")
    with Image.open(io.BytesIO(raw)) as im:
        if im.width > 8192 or im.height > 8192:
            raise ValueError("Texture exceeds 8192 pixels per dimension")
        return im.convert("RGBA")
