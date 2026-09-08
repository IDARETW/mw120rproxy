"""CoD4 IWI decoding and static sky-enclosure geometry used by the map builders."""

import io, struct
from PIL import Image


def decode_iwi(data):
    # CoD4 v6 header and cube flag are documented by mjkzy/x64-zt utils/iwi.
    # Mips run smallest to largest. The last mip contains all six cube faces.
    if len(data) < 28 or data[:4] != b"IWi\x06":
        raise ValueError("Expected a CoD4 v6 IWI image")
    fmt, flags = data[4:6]
    width, height, depth = struct.unpack_from("<3H", data, 6)
    file_sizes = struct.unpack_from("<4I", data, 12)
    if fmt not in (1, 2, 3, 4, 5, 11, 12, 13) or flags & 8 or depth not in (0, 1):
        raise ValueError(f"Unsupported 2D/cube IWI format {fmt}")
    if not 1 <= width <= 4096 or not 1 <= height <= 4096:
        raise ValueError("Invalid IWI dimensions")
    faces = 6 if flags & 4 else 1
    if faces == 6 and width != height:
        raise ValueError("Cubemap faces must be square")
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
