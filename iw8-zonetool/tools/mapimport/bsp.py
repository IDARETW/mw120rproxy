"""Explicit little-endian BSP adapters, based on the id Software released headers."""

import struct
from itertools import pairwise

from .core import (
    Scene,
    cross,
    entities,
    hull_from_planes,
    read_bytes,
    sub,
    triangulate,
    unit,
)


class BSP:
    def __init__(self, path):
        self.data = read_bytes(path)
        if len(self.data) < 8:
            raise ValueError("Truncated BSP header")
        magic, self.version = struct.unpack_from("<4si", self.data)
        if magic != b"IBSP":
            raise ValueError("Expected little-endian IBSP")
        count = {38: 19, 46: 17, 47: 17}.get(self.version)
        if not count:
            raise ValueError(
                f"Unsupported IBSP version {self.version}; supported: Quake II 38, Quake III 46, Quake Live 47"
            )
        end = 8 + 8 * count
        if len(self.data) < end:
            raise ValueError("Truncated BSP directory")
        self.lumps = []
        spans = []
        for i in range(count):
            offset, size = struct.unpack_from("<ii", self.data, 8 + 8 * i)
            if offset < 0 or size < 0 or offset + size > len(self.data) or (size and offset < end):
                raise ValueError(f"Invalid BSP lump {i} range")
            if size:
                spans.append((offset, offset + size, i))
            self.lumps.append(memoryview(self.data)[offset : offset + size])
        spans.sort()
        if any(a[1] > b[0] for a, b in pairwise(spans)):
            raise ValueError("Overlapping BSP lumps")

    def rows(self, index, fmt):
        size = struct.calcsize(fmt)
        if len(self.lumps[index]) % size:
            raise ValueError(f"BSP lump {index} is not a multiple of record size {size}")
        return list(struct.iter_unpack(fmt, self.lumps[index]))


def checked(rows, first, count, label):
    if first < 0 or count < 0 or first + count > len(rows):
        raise ValueError(f"Invalid {label} range")
    return rows[first : first + count]


def item(rows, index, label):
    if index < 0 or index >= len(rows):
        raise ValueError(f"Invalid {label} index: {index}")
    return rows[index]


def cstr(value):
    return bytes(value).split(b"\0", 1)[0].decode("utf-8", errors="strict")


def read_q3(path, patch_steps=6):
    bsp = BSP(path)
    if bsp.version not in (46, 47):
        raise ValueError("Not a Quake III / Quake Live BSP")
    if not 1 <= patch_steps <= 32:
        raise ValueError("Patch steps must be 1..32")
    scene = Scene("q3" if bsp.version == 46 else "ql")
    scene.entities = entities(bytes(bsp.lumps[0]).decode("utf-8"))
    shaders = bsp.rows(1, "<64sii")
    planes = bsp.rows(2, "<4f")
    models = bsp.rows(7, "<6f4i")
    brushes = bsp.rows(8, "<3i")
    sides = bsp.rows(9, "<2i")
    verts = bsp.rows(10, "<10f4B")
    meshverts = [r[0] for r in bsp.rows(11, "<i")]
    faces = bsp.rows(13, "<12i12f2i")
    if not models:
        raise ValueError("BSP has no world model")
    vertex = lambda v: {
        "position": list(v[:3]),
        "uv": list(v[3:5]),
        "normal": list(v[7:10]),
        "lightmap_uv": list(v[5:7]),
        "color": list(v[10:14]),
    }
    patch_count = 0
    # Each compiled model is included once. Movers are frozen at their authored
    # origin; rotation is rejected rather than silently producing displaced solids.
    placements = {
        int(e["model"][1:]): e
        for e in scene.entities
        if e.get("model", "").startswith("*") and e["model"][1:].isdigit()
    }
    for mi, model in enumerate(models):
        ent = placements.get(mi, {})
        if mi and ent.get("classname", "").startswith("trigger_"):
            continue
        from .core import add, vec

        origin = vec(ent.get("origin", "0 0 0")) if mi else [0, 0, 0]
        if mi and (ent.get("angles", "0 0 0") != "0 0 0" or ent.get("angle", "0") != "0"):
            raise ValueError(
                "Rotated Quake brush entities require baking transforms in the source map"
            )
        for face in checked(faces, model[6], model[7], "model faces"):
            shader = item(shaders, face[0], "shader")
            name = cstr(shader[0])
            flags, contents = shader[1:]
            scene.materials.setdefault(name, {"texture": name})
            # SURF_NODRAW and SURF_SKY are not opaque world walls.
            if flags & (0x80 | 0x4):
                if flags & 4:
                    scene.warn(
                        "Quake sky shader surfaces are omitted; a Replay sky material is not synthesized."
                    )
                continue
            kind, first, count, base, nindices = face[2:7]
            source = checked(verts, first, count, "face vertices")
            if kind in (1, 3):
                indices = checked(meshverts, base, nindices, "mesh indices")
                if len(indices) % 3:
                    raise ValueError("Incomplete Quake triangle list")
                if any(x < 0 or x >= count for x in indices):
                    raise ValueError("Quake local mesh index out of bounds")
                output = [vertex(v) for v in source]
            elif kind == 2:
                width, height = face[-2:]
                if (
                    width < 3
                    or height < 3
                    or width % 2 != 1
                    or height % 2 != 1
                    or width * height != count
                ):
                    raise ValueError("Invalid quadratic patch dimensions")
                output, indices = [], []
                weights = lambda t: ((1 - t) ** 2, 2 * t * (1 - t), t * t)
                for py in range(0, height - 2, 2):
                    for px in range(0, width - 2, 2):
                        start = len(output)
                        for y in range(patch_steps + 1):
                            for x in range(patch_steps + 1):
                                wx, wy = (
                                    weights(x / patch_steps),
                                    weights(y / patch_steps),
                                )
                                values = [
                                    sum(
                                        source[(py + j) * width + px + i][k] * wx[i] * wy[j]
                                        for j in range(3)
                                        for i in range(3)
                                    )
                                    for k in range(14)
                                ]
                                values[7:10] = unit(values[7:10])
                                output.append(vertex(values))
                        stride = patch_steps + 1
                        for y in range(patch_steps):
                            for x in range(patch_steps):
                                a = start + y * stride + x
                                indices.extend(
                                    (
                                        a,
                                        a + stride,
                                        a + 1,
                                        a + 1,
                                        a + stride,
                                        a + stride + 1,
                                    )
                                )
                patch_count += 1
            elif kind == 4:
                scene.warn(
                    "Quake billboard faces are omitted; camera-facing effects require runtime conversion."
                )
                continue
            else:
                raise ValueError(f"Unsupported Quake face type {kind}")
            if not indices:
                continue
            for v in output:
                v["position"] = add(v["position"], origin)
            surface = {
                "material": name,
                "vertices": output,
                "indices": indices,
                "nonsolid": not bool(contents & (1 | 0x10000)),
            }
            # q3 mesh winding follows authored vertex normals. Normalize to CCW
            # before the shared Replay writer performs its clockwise conversion.
            align_winding(surface)
            scene.surfaces.append(surface)
            if kind == 2 and not surface["nonsolid"]:
                patch = Scene(scene.engine, surfaces=[surface])
                patch.triangle_collision()
                scene.hulls.extend(patch.hulls)
        for brush in checked(brushes, model[8], model[9], "model brushes"):
            shader = item(shaders, brush[2], "brush shader")
            if not shader[2] & (1 | 0x10000):
                continue  # solid or playerclip
            ps = [
                item(planes, s[0], "brush plane")
                for s in checked(sides, brush[0], brush[1], "brush sides")
            ]
            scene.hulls.append([add(p, origin) for p in hull_from_planes(ps)])
    scene.stats["quadratic_patches"] = patch_count
    scene.warn(
        "Quake shader programs, pickups, triggers, movers, bots and game rules are not ported; compatible player spawns are translated to TDM."
    )
    if len(bsp.lumps[14]):
        scene.warn(
            "Quake baked lightmap UVs are preserved in scene.json; source lightmap lighting is not baked into the Replay shader."
        )
    return scene


def align_winding(surface):
    from .core import add, dot

    v, ix = surface["vertices"], surface["indices"]
    for i in range(0, len(ix), 3):
        a, b, c = [v[k] for k in ix[i : i + 3]]
        n = cross(sub(b["position"], a["position"]), sub(c["position"], a["position"]))
        if dot(n, add(add(a["normal"], b["normal"]), c["normal"])) < 0:
            ix[i + 1], ix[i + 2] = ix[i + 2], ix[i + 1]


def read_q2(path):
    bsp = BSP(path)
    if bsp.version != 38:
        raise ValueError("Not a Quake II BSP")
    scene = Scene("q2")
    scene.entities = entities(bytes(bsp.lumps[0]).decode("utf-8"))
    planes = bsp.rows(1, "<4fi")
    verts = bsp.rows(2, "<3f")
    texinfo = bsp.rows(5, "<8fii32si")
    faces = bsp.rows(6, "<Hhihh4Bi")
    edges = bsp.rows(11, "<2H")
    surfedges = [r[0] for r in bsp.rows(12, "<i")]
    models = bsp.rows(13, "<9f3i")
    brushes = bsp.rows(14, "<3i")
    sides = bsp.rows(15, "<Hh")
    if not models:
        raise ValueError("Quake II BSP has no world model")
    if len(models) > 1:
        scene.warn(
            "Quake II brush models remain in their compiled pose; dynamic entity logic is not executed."
        )
    for face in faces:
        ti = item(texinfo, face[4], "texinfo")
        flags, name = ti[8], cstr(ti[10])
        if flags & (4 | 128):
            continue  # SKY / NODRAW
        # Texture resolution is resolved during image lookup; WAL defaults are
        # never guessed. Keep texel coordinates until packaging.
        scene.materials.setdefault(name, {"texture": "textures/" + name, "uv_texels": True})
        points = []
        for e in checked(surfedges, face[2], face[3], "face edges"):
            edge = item(edges, abs(e), "edge")
            points.append(list(item(verts, edge[0 if e >= 0 else 1], "vertex")))
        if len(points) < 3:
            raise ValueError("Quake II face has fewer than three vertices")
        p = item(planes, face[0], "face plane")
        n = [x * (-1 if face[1] else 1) for x in p[:3]]
        output = [
            {
                "position": v,
                "normal": n,
                "uv": [
                    sum(v[k] * ti[k] for k in range(3)) + ti[3],
                    sum(v[k] * ti[4 + k] for k in range(3)) + ti[7],
                ],
            }
            for v in points
        ]
        s = {
            "material": name,
            "vertices": output,
            "indices": triangulate(points),
            "nonsolid": bool(flags & (16 | 32)),
        }
        align_winding(s)
        scene.surfaces.append(s)
    for brush in brushes:
        if not brush[2] & (1 | 0x10000):
            continue
        scene.hulls.append(
            hull_from_planes(
                [
                    item(planes, s[0], "brush plane")[:4]
                    for s in checked(sides, brush[0], brush[1], "brush sides")
                ]
            )
        )
    scene.warn(
        "Quake II lightmaps, animated textures, models, triggers and game rules are not converted."
    )
    return scene
