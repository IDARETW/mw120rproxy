"""Valve Source BSP v19/v20 static architecture and displacement terrain."""

import lzma
import struct
from itertools import pairwise

from .bsp import align_winding, checked, item
from .core import (
    Scene,
    add,
    cross,
    dot,
    entities,
    hull_from_planes,
    mul,
    read_bytes,
    sub,
    triangulate,
    unit,
)


def displacement_indices(edge):
    """Full-resolution topology: each 2x2 block fans around its center vertex.

    Valve DispCommon_GenerateTriIndices_R uses this alternating diagonal pattern.
    A uniform diagonal changes the shape of non-planar displacement cells.
    """
    indices = []
    for y in range(edge - 1):
        for x in range(edge - 1):
            a = y * edge + x
            b, c, d = a + 1, a + edge, a + edge + 1
            if (x + y) % 2:
                indices.extend((a, c, b, b, c, d))
            else:
                indices.extend((a, c, d, a, d, b))
    return indices


def read_source(path):
    data = read_bytes(path)
    if len(data) < 1036 or data[:4] != b"VBSP":
        raise ValueError("Truncated Source BSP")
    version = struct.unpack_from("<i", data, 4)[0]
    if version not in (19, 20):
        raise ValueError(f"Unsupported Source BSP version {version}; supported: 19 and 20")
    lumps = []
    versions = []
    spans = []
    for i in range(64):
        offset, size, v, unpacked = struct.unpack_from("<iiiI", data, 8 + 16 * i)
        if min(offset, size) < 0 or offset + size > len(data) or (size and offset < 1036):
            raise ValueError(f"Invalid Source lump {i}")
        if size:
            spans.append((offset, offset + size))
        raw = data[offset : offset + size]
        if unpacked:
            if len(raw) < 17 or raw[:4] != b"LZMA":
                raise ValueError("Invalid Source compressed lump")
            actual, compressed = struct.unpack_from("<II", raw, 4)
            if actual != unpacked or actual > 128 * 1024 * 1024 or compressed != len(raw) - 17:
                raise ValueError("Source LZMA size mismatch")
            prop = raw[12]
            lc = prop % 9
            rest = prop // 9
            lp = rest % 5
            pb = rest // 5
            dictionary = struct.unpack_from("<I", raw, 13)[0]
            if dictionary > 64 * 1024 * 1024 or lc + lp > 4 or pb > 4:
                raise ValueError("Source LZMA memory limit")
            decoder = lzma.LZMADecompressor(
                format=lzma.FORMAT_RAW,
                filters=[
                    {
                        "id": lzma.FILTER_LZMA1,
                        "dict_size": max(4096, dictionary),
                        "lc": lc,
                        "lp": lp,
                        "pb": pb,
                    }
                ],
            )
            raw = decoder.decompress(raw[17:], max_length=actual + 1)
            if len(raw) != actual:
                raise ValueError("Source decompressed lump length mismatch")
        lumps.append(raw)
        versions.append(v)
    spans.sort()
    if any(a[1] > b[0] for a, b in pairwise(spans)):
        raise ValueError("Overlapping Source lumps")

    def rows(i, fmt):
        if len(lumps[i]) % struct.calcsize(fmt):
            raise ValueError(f"Invalid Source lump {i} record length")
        return list(struct.iter_unpack(fmt, lumps[i]))

    scene = Scene("source")
    scene.entities = entities(lumps[0].decode("utf-8"))
    planes = rows(1, "<4fi")
    texdata = rows(2, "<3f5i")
    vertices = rows(3, "<3f")
    info = rows(6, "<16f2i")
    faces = rows(7, "<HBBihhhh4Bif2i2iiHHI")
    edges = rows(12, "<2H")
    surfedges = [r[0] for r in rows(13, "<i")]
    models = rows(14, "<9f3i")
    brushes = rows(18, "<3i")
    sides = rows(19, "<Hhhh")
    names = [r[0] for r in rows(44, "<i")]
    disps = rows(26, "<176s")
    dispverts = rows(33, "<5f")
    if versions[7] != 1:
        raise ValueError("Unsupported Source face lump layout; expected version 1")
    if not models:
        raise ValueError("Source map has no world model")
    placements = {
        int(e["model"][1:]): e
        for e in scene.entities
        if e.get("model", "").startswith("*") and e["model"][1:].isdigit()
    }

    for mi, model in enumerate(models):
        ent = placements.get(mi, {})
        if mi and ent.get("classname", "").startswith("trigger_"):
            continue
        if mi and (ent.get("angles", "0 0 0") != "0 0 0" or ent.get("origin", "0 0 0") != "0 0 0"):
            raise ValueError("Source brush entity transforms require baking before import")
        for f in checked(faces, model[10], model[11], "Source model faces"):
            ti = item(info, f[5], "Source texinfo")
            td = item(texdata, ti[17], "Source texdata")
            flags = ti[16]
            if flags & (0x80 | 0x4 | 0x2):
                continue  # nodraw / sky / sky2d
            offset = item(names, td[3], "texture name")
            if not 0 <= offset < len(lumps[43]):
                raise ValueError("Source texture name offset out of bounds")
            end = lumps[43].find(b"\0", offset)
            if end < 0:
                raise ValueError("Unterminated Source texture name")
            name = lumps[43][offset:end].decode("utf-8")
            width, height = td[4:6]
            if width <= 0 or height <= 0:
                raise ValueError("Invalid Source texture dimensions")
            scene.materials.setdefault(name, {"texture": "materials/" + name, "source_vmt": True})
            ps = []
            for e in checked(surfedges, f[3], f[4], "Source surface edges"):
                edge = item(edges, abs(e), "Source edge")
                ps.append(list(item(vertices, edge[0 if e >= 0 else 1], "Source vertex")))
            plane = item(planes, f[0], "Source face plane")
            normal = mul(plane[:3], -1 if f[1] else 1)
            if f[6] >= 0:
                raw = item(disps, f[6], "displacement")[0]
                start = list(struct.unpack_from("<3f", raw))
                first, power = (
                    struct.unpack_from("<i", raw, 12)[0],
                    struct.unpack_from("<i", raw, 20)[0],
                )
                if len(ps) != 4 or power not in (2, 3, 4):
                    raise ValueError("Unsupported displacement control surface")
                nearest = min(range(4), key=lambda i: dot(sub(ps[i], start), sub(ps[i], start)))
                if dot(sub(ps[nearest], start), sub(ps[nearest], start)) > 0.01:
                    raise ValueError("Displacement start is not a face corner")
                ps = ps[nearest:] + ps[:nearest]
                edge = (1 << power) + 1
                dv = checked(dispverts, first, edge * edge, "displacement vertices")
                grid = []
                for y in range(edge):
                    for x in range(edge):
                        u, v = x / (edge - 1), y / (edge - 1)
                        p = add(
                            add(mul(ps[0], (1 - u) * (1 - v)), mul(ps[3], u * (1 - v))),
                            add(mul(ps[1], (1 - u) * v), mul(ps[2], u * v)),
                        )
                        d = dv[y * edge + x]
                        grid.append(add(p, mul(d[:3], d[3])))
                ps = grid
                indices = displacement_indices(edge)
                normals = [[0.0, 0.0, 0.0] for p in ps]
                for j in range(0, len(indices), 3):
                    a, b, c = [ps[k] for k in indices[j : j + 3]]
                    n = cross(sub(b, a), sub(c, a))
                    if dot(n, normal) < 0:
                        n = mul(n, -1)
                    for k in indices[j : j + 3]:
                        normals[k] = add(normals[k], n)
                normals = [unit(n) for n in normals]
            else:
                indices = triangulate(ps)
                normals = [normal for p in ps]
            output = [
                {
                    "position": p,
                    "normal": n,
                    "uv": [
                        (dot(p, ti[:3]) + ti[3]) / width,
                        (dot(p, ti[4:7]) + ti[7]) / height,
                    ],
                }
                for p, n in zip(ps, normals)
            ]
            s = {"material": name, "vertices": output, "indices": indices}
            align_winding(s)
            scene.surfaces.append(s)
            if f[6] >= 0:
                patch = Scene("source", surfaces=[s])
                patch.triangle_collision()
                scene.hulls.extend(patch.hulls)
    for brush in brushes:
        if not brush[2] & (1 | 0x10000):
            continue
        bs = checked(sides, brush[0], brush[1], "Source brush sides")
        # VBSP leaves dbrushside.dispinfo zero on ordinary solid brushes. The
        # displacement control brush is removed by VBSP itself; do not mistake
        # this unused field for the dface dispinfo sentinel and lose all walls.
        scene.hulls.append(
            hull_from_planes([item(planes, s[0], "Source brush plane")[:4] for s in bs])
        )
    if lumps[40]:
        scene.stats["embedded_pak_bytes"] = len(lumps[40])
        scene._pak = lumps[40]
    scene.warn(
        "Source static props, overlays, lightmaps, VMT shader behavior, displacement blend alpha/triangle tags, entities and physics are not converted; world brushes and full-resolution displacements are imported."
    )
    return scene
