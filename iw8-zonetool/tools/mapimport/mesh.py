"""Wavefront OBJ/MTL and glTF 2.0/GLB static-scene adapters."""

import base64
import json
import math
import shlex
import struct
from pathlib import Path

from .core import (
    Scene,
    cross,
    dot,
    read_bytes,
    safe_path,
    sha256,
    sub,
    triangulate,
    unit,
    vec,
)


def obj_index(value, count):
    value = int(value)
    index = value - 1 if value > 0 else count + value
    if not value or not 0 <= index < count:
        raise ValueError("OBJ index is zero or out of range")
    return index


def read_obj(path, material_libraries=True):
    path = Path(path)
    scene = Scene("obj")
    positions, uvs, normals = [], [], []
    material, active = "default", None
    for line in read_bytes(path).decode("utf-8-sig").splitlines():
        row = line.strip().split()
        if not row or row[0].startswith("#"):
            continue
        op, values = row[0], row[1:]
        if op == "v":
            if len(values) not in (3, 4, 6):
                raise ValueError("Unsupported OBJ vertex record")
            p = vec(values[:3])
            if len(values) == 4:
                w = float(values[3])
                if not math.isfinite(w) or not w:
                    raise ValueError("Invalid OBJ homogeneous coordinate")
                p = [x / w for x in p]
            positions.append(p)
        elif op == "vt":
            uvs.append(vec(values[:2], 2))
        elif op == "vn":
            normals.append(vec(values))
        elif op == "usemtl":
            material = " ".join(values)
            if not material:
                raise ValueError("Empty OBJ material name")
            active = None
        elif op == "mtllib":
            if not material_libraries:
                continue
            for name in shlex.split(line.strip()[len(op) :], posix=True):
                mtl = safe_path(path.parent, name)
                scene.dependencies[str(mtl)] = sha256(mtl)
                read_mtl(mtl, scene)
        elif op == "f":
            if len(values) < 3:
                raise ValueError("OBJ face has fewer than three vertices")
            refs = [v.split("/") for v in values]
            if any(len(v) > 3 for v in refs):
                raise ValueError("Invalid OBJ face reference")
            points = [positions[obj_index(r[0], len(positions))] for r in refs]
            if len(points) == 3:
                n = cross(sub(points[1], points[0]), sub(points[2], points[0]))
                if dot(n, n) < 1e-12:
                    scene.stats["degenerate_faces_removed"] = (
                        scene.stats.get("degenerate_faces_removed", 0) + 1
                    )
                    scene.warn(
                        "Zero-area OBJ triangles were removed; their count is recorded in statistics."
                    )
                    continue
                indices = [0, 1, 2]
            else:
                indices = triangulate(points)
            fallback = unit(
                cross(
                    sub(points[indices[1]], points[indices[0]]),
                    sub(points[indices[2]], points[indices[0]]),
                )
            )
            output = []
            for r, p in zip(refs, points):
                uv = uvs[obj_index(r[1], len(uvs))] if len(r) > 1 and r[1] else [0, 0]
                n = normals[obj_index(r[2], len(normals))] if len(r) > 2 and r[2] else fallback
                if dot(n, n) < 1e-12:
                    n = fallback
                    scene.stats["zero_normals_rebuilt"] = (
                        scene.stats.get("zero_normals_rebuilt", 0) + 1
                    )
                    scene.warn(
                        "Zero OBJ normals were rebuilt from their face geometry; the count is recorded in statistics."
                    )
                else:
                    n = unit(n)
                # Canonical images use a top-left origin; OBJ uses bottom-left.
                output.append({"position": list(p), "normal": list(n), "uv": [uv[0], 1 - uv[1]]})
            if active is None:
                active = {"material": material, "vertices": [], "indices": []}
                scene.surfaces.append(active)
            start = len(active["vertices"])
            active["vertices"].extend(output)
            active["indices"].extend(start + i for i in indices)
        elif op in ("curv", "curv2", "surf", "vp", "cstype"):
            raise ValueError("OBJ free-form curves/surfaces must be tessellated before import")
        elif op not in ("o", "g", "s", "l", "p", "#"):
            scene.warn(f"OBJ directive {op!r} is not converted.")
    scene.warn(
        "OBJ contains no gameplay entities; supply --spawn x y z. Mesh collision is generated from visible triangles."
    )
    return scene


def read_mtl(path, scene):
    current = None
    for line in read_bytes(path).decode("utf-8-sig").splitlines():
        words = line.strip().split()
        if not words or words[0].startswith("#"):
            continue
        key, values = words[0], words[1:]
        if key == "newmtl":
            current = {}
            scene.materials[" ".join(values)] = current
        elif current is not None:
            if key == "Kd":
                current["color"] = vec(values)
            elif key in ("d", "Tr"):
                alpha = float(values[0])
                current["alpha"] = 1 - alpha if key == "Tr" else alpha
                if current["alpha"] < 1:
                    scene.warn(
                        "MTL translucency is reported but rendered opaque by the static import material."
                    )
            elif key == "map_Kd":
                name = line.strip()[len(key) :].strip().strip('"')
                if name.startswith("-"):
                    raise ValueError("MTL texture options require baking; use a plain map_Kd path")
                texture = safe_path(path.parent, name)
                current["texture_path"] = str(texture)
            elif key in ("map_Bump", "bump", "norm", "map_Ks"):
                scene.warn(
                    "Normal, bump and specular texture channels are not converted by the static mesh material."
                )


def matmul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


IDENTITY = [[float(r == c) for c in range(4)] for r in range(4)]


def node_matrix(node):
    if "matrix" in node:
        if any(k in node for k in ("translation", "rotation", "scale")):
            raise ValueError("glTF node mixes matrix and TRS")
        v = vec(node["matrix"], 16)
        if [v[k] for k in (3, 7, 11, 15)] != [0, 0, 0, 1]:
            raise ValueError("Non-affine glTF node matrix")
        return [[v[c * 4 + r] for c in range(4)] for r in range(4)]
    x, y, z, w = vec(node.get("rotation", [0, 0, 0, 1]), 4)
    if abs(x * x + y * y + z * z + w * w - 1) > 0.001:
        raise ValueError("Non-unit glTF node quaternion")
    s = vec(node.get("scale", [1, 1, 1]))
    t = vec(node.get("translation", [0, 0, 0]))
    if any(abs(v) < 1e-9 for v in s):
        raise ValueError("Singular glTF node scale")
    r = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]
    return [[r[i][j] * s[j] for j in range(3)] + [t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def read_gltf(path):
    path = Path(path)
    raw = read_bytes(path)
    binary = None
    if raw[:4] == b"glTF":
        if len(raw) < 20:
            raise ValueError("Truncated GLB")
        _magic, version, length = struct.unpack_from("<4sII", raw)
        if version != 2 or length != len(raw):
            raise ValueError("Invalid GLB version/length")
        p = 12
        chunks = []
        while p < len(raw):
            if p + 8 > len(raw):
                raise ValueError("Truncated GLB chunk")
            size, kind = struct.unpack_from("<I4s", raw, p)
            p += 8
            if size % 4 or p + size > len(raw):
                raise ValueError("Invalid GLB chunk range")
            chunks.append((kind, raw[p : p + size]))
            p += size
        if not chunks or chunks[0][0] != b"JSON" or len(chunks) > 2:
            raise ValueError("Unsupported GLB chunks")
        if len(chunks) == 2:
            if chunks[1][0] != b"BIN\0":
                raise ValueError("Expected GLB BIN chunk")
            binary = chunks[1][1]
        doc = json.loads(chunks[0][1])
    else:
        doc = json.loads(raw)
    if doc.get("asset", {}).get("version") != "2.0":
        raise ValueError("Only glTF 2.0 is supported")
    if doc.get("extensionsRequired"):
        raise ValueError(
            "glTF required extensions are unsupported: " + str(doc["extensionsRequired"])
        )
    scene = Scene("gltf")
    buffers = []

    def uri(value):
        if value.startswith("data:"):
            header, payload = value.split(",", 1)
            if not header.endswith(";base64"):
                raise ValueError("Only base64 glTF data URIs are supported")
            return base64.b64decode(payload, validate=True)
        from urllib.parse import unquote

        asset = safe_path(path.parent, unquote(value))
        scene.dependencies[str(asset)] = sha256(asset)
        return read_bytes(asset)

    for i, b in enumerate(doc.get("buffers", [])):
        data = uri(b["uri"]) if "uri" in b else binary if i == 0 else None
        size = b["byteLength"]
        if (
            data is None
            or type(size) != int
            or size < 0
            or len(data) < size
            or len(data) - size > 3
        ):
            raise ValueError("glTF buffer length mismatch")
        buffers.append(data[:size])

    def view(i):
        from .bsp import item

        v = item(doc.get("bufferViews", []), i, "bufferView")
        data = item(buffers, v["buffer"], "buffer")
        start = v.get("byteOffset", 0)
        size = v["byteLength"]
        if start < 0 or size < 0 or start + size > len(data):
            raise ValueError("glTF bufferView out of bounds")
        return data[start : start + size], v

    def accessor(i):
        from .bsp import item

        a = item(doc.get("accessors", []), i, "accessor")
        if "sparse" in a or "bufferView" not in a:
            raise ValueError("Sparse/implicit glTF accessors require baking")
        fmt = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}.get(
            a["componentType"]
        )
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}.get(a["type"])
        if not fmt or not n:
            raise ValueError("Unsupported glTF accessor format")
        data, v = view(a["bufferView"])
        size = struct.calcsize("<" + fmt) * n
        stride = v.get("byteStride", size)
        offset = a.get("byteOffset", 0)
        count = a["count"]
        if (
            type(count) != int
            or not 0 <= count <= 2_000_000
            or stride < size
            or offset < 0
            or (count and offset + (count - 1) * stride + size > len(data))
        ):
            raise ValueError("glTF accessor out of bounds")
        values = [
            list(struct.unpack_from("<" + fmt * n, data, offset + j * stride)) for j in range(count)
        ]
        if a.get("normalized"):
            maximum = {5120: 127, 5121: 255, 5122: 32767, 5123: 65535}.get(a["componentType"])
            if not maximum:
                raise ValueError("Invalid normalized glTF component type")
            values = [[max(-1, v / maximum) for v in row] for row in values]
        return values

    for i, m in enumerate(doc.get("materials", [])):
        pbr = m.get("pbrMetallicRoughness", {})
        material = {"color": vec(pbr.get("baseColorFactor", [1, 1, 1, 1]), 4)[:3]}
        if "baseColorTexture" in pbr:
            ref = pbr["baseColorTexture"]
            if ref.get("texCoord", 0) != 0 or ref.get("extensions"):
                raise ValueError("glTF texture transforms/alternate UV sets require baking")
            from .bsp import item

            texture = item(doc.get("textures", []), ref["index"], "texture")
            image = item(doc.get("images", []), texture["source"], "image")
            data = uri(image["uri"]) if "uri" in image else view(image["bufferView"])[0]
            material["image_bytes"] = data
        scene.materials[str(i)] = material
        if m.get("alphaMode", "OPAQUE") != "OPAQUE":
            scene.warn(
                "glTF alpha modes are reported but rendered opaque by the static import material."
            )
    if doc.get("animations") or doc.get("skins"):
        scene.warn("glTF animations and skins are not converted; static nodes only.")
    nodes = doc.get("nodes", [])
    from .bsp import item

    root = item(doc.get("scenes", []), doc.get("scene", 0), "scene")

    def visit(i, parent, ancestors):
        if i in ancestors or len(ancestors) > 128:
            raise ValueError("glTF node cycle/depth limit")
        node = item(nodes, i, "node")
        matrix = matmul(parent, node_matrix(node))
        if "skin" in node or node.get("weights"):
            raise ValueError("Skinned or morphed nodes must be baked to a static mesh")
        a, b, c = [row[:3] for row in matrix[:3]]
        det = dot(a, cross(b, c))
        if abs(det) < 1e-12:
            raise ValueError("Singular glTF world transform")
        # Cofactor matrix / determinant = inverse transpose, for nonuniform scales.
        normal_matrix = [[x / det for x in row] for row in (cross(b, c), cross(c, a), cross(a, b))]
        if "mesh" in node:
            for primitive in item(doc.get("meshes", []), node["mesh"], "mesh")["primitives"]:
                if primitive.get("mode", 4) != 4 or primitive.get("targets"):
                    raise ValueError("glTF primitive must be static TRIANGLES")
                attrs = primitive["attributes"]
                pos = accessor(attrs["POSITION"])
                if any(len(p) != 3 for p in pos):
                    raise ValueError("glTF POSITION must be VEC3")
                uv = (
                    accessor(attrs["TEXCOORD_0"])
                    if "TEXCOORD_0" in attrs
                    else [[0, 0] for p in pos]
                )
                normals = accessor(attrs["NORMAL"]) if "NORMAL" in attrs else None
                if len(uv) != len(pos) or (normals is not None and len(normals) != len(pos)):
                    raise ValueError("glTF attribute counts differ")
                indices = (
                    [v[0] for v in accessor(primitive["indices"])]
                    if "indices" in primitive
                    else list(range(len(pos)))
                )
                if len(indices) % 3 or any(
                    type(i) != int or not 0 <= i < len(pos) for i in indices
                ):
                    raise ValueError("Invalid glTF triangle indices")
                if normals is None:
                    normals = [[0.0, 0.0, 0.0] for p in pos]
                    for j in range(0, len(indices), 3):
                        x, y, z = [pos[k] for k in indices[j : j + 3]]
                        n = cross(sub(y, x), sub(z, x))
                        for k in indices[j : j + 3]:
                            normals[k] = [normals[k][q] + n[q] for q in range(3)]
                output = []
                for p, n, t in zip(pos, normals, uv):
                    p = vec(p)
                    n = vec(n)
                    output.append(
                        {
                            "position": [dot(row[:3], p) + row[3] for row in matrix[:3]],
                            "normal": unit([dot(row, n) for row in normal_matrix]),
                            "uv": vec(t, 2),
                        }
                    )
                if det < 0:
                    for j in range(0, len(indices), 3):
                        indices[j + 1], indices[j + 2] = indices[j + 2], indices[j + 1]
                scene.surfaces.append(
                    {
                        "material": str(primitive.get("material", "default")),
                        "vertices": output,
                        "indices": indices,
                    }
                )
        for child in node.get("children", []):
            visit(child, matrix, ancestors | {i})

    for i in root.get("nodes", []):
        visit(i, IDENTITY, set())
    # glTF specifies Y-up meters. The CLI supplies default meters-to-CoD-inches.
    scene.transform(1, "y")
    scene.warn(
        "glTF base-color textures and node transforms are imported; PBR lighting, skins and gameplay are not ported."
    )
    return scene
