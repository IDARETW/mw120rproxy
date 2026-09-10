"""Bake convex world collision into a Replay HavokPhysicsShapeList."""

import argparse
from collections import Counter, defaultdict
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import struct

from replay_havok import ReplayHavok


def read_brushes(path):
    data = path.read_bytes()
    if len(data) < 12 or data[:8] not in (b"MWCOLL02", b"MWCOLL03"):
        raise ValueError("Expected MWCOLL02 or MWCOLL03 convex collision")
    tagged = data[:8] == b"MWCOLL03"
    count = struct.unpack_from("<I", data, 8)[0]
    if not 0 < count <= 32768:
        raise ValueError("Invalid collision hull count")
    brushes, offset = [], 12
    for _ in range(count):
        vertices = struct.unpack_from("<I", data, offset)[0]
        contents = struct.unpack_from("<I", data, offset + 4)[0] if tagged else 1
        offset += 8 if tagged else 4
        if not 4 <= vertices <= 252 or not contents or contents & ~0x33681:
            raise ValueError("Unsupported hull vertices or collision contents")
        points = list(struct.iter_unpack("<3f", data[offset : offset + vertices * 12]))
        offset += vertices * 12
        if len(points) != vertices or any(not math.isfinite(v) for p in points for v in p):
            raise ValueError("Invalid collision vertex data")
        brushes.append((points, contents))
    if offset != len(data):
        raise ValueError("Trailing collision data")
    return brushes


class FloorMaterials:
    def __init__(self, path):
        self.cells = defaultdict(list)
        if path is None:
            return
        data = path.read_bytes()
        if data[:8] != b"MWRSTEP1" or len(data) != 12 + 40 * struct.unpack_from("<I", data, 8)[0]:
            raise ValueError("Invalid floor material triangles")
        for record in struct.iter_unpack("<9fI", data[12:]):
            a, b, c, material = record[:3], record[3:6], record[6:9], record[9]
            if not 1 <= material <= 28 or any(not math.isfinite(v) for v in record[:9]):
                raise ValueError("Invalid floor material")
            u, v = [b[k] - a[k] for k in range(3)], [c[k] - a[k] for k in range(3)]
            determinant = u[0] * v[1] - u[1] * v[0]
            if abs(determinant) < 0.001:
                continue
            x0, x1 = math.floor(min(a[0], b[0], c[0]) / 128), math.floor(
                max(a[0], b[0], c[0]) / 128
            )
            y0, y1 = math.floor(min(a[1], b[1], c[1]) / 128), math.floor(
                max(a[1], b[1], c[1]) / 128
            )
            if (x1 - x0 + 1) * (y1 - y0 + 1) > 100000:
                raise ValueError("Floor triangle exceeds map limits")
            for x in range(x0, x1 + 1):
                for y in range(y0, y1 + 1):
                    self.cells[x, y].append((a, u, v, determinant, material))

    def at(self, x, y, z):
        best, material = 12.01, 5
        for a, u, v, determinant, candidate in self.cells[math.floor(x / 128), math.floor(y / 128)]:
            dx, dy = x - a[0], y - a[1]
            s = (dx * v[1] - dy * v[0]) / determinant
            t = (u[0] * dy - u[1] * dx) / determinant
            if s < -0.002 or t < -0.002 or s + t > 1.002:
                continue
            distance = abs(z - a[2] - s * u[2] - t * v[2])
            if distance < best:
                best, material = distance, candidate
        return material


def bake(executable, collision, footsteps, destination):
    brushes = read_brushes(collision)
    floors = FloorMaterials(footsteps)
    hk = ReplayHavok(executable)
    config = hk.alloc(96)
    hk.function(0x1E81300, C.c_void_p, C.c_void_p)(config)
    hk.put(config, "fB", 0, 0)
    hk.put(config + 16, "BB", 1, 0)  # Faces for queries, no unused mass properties.
    hulls, tag_indices, tags, materials = [], {}, [], Counter()
    instances = hk.alloc(len(brushes) * 112)
    minimum, maximum = [math.inf] * 3, [-math.inf] * 3
    all_contents, total_vertices, total_triangles = 0, 0, 0
    vertices, vertex_array = hk.alloc(252 * 16), hk.alloc(16)
    for index, (points, contents) in enumerate(brushes):
        center = [sum(p[k] for p in points) / len(points) for k in range(3)]
        material = floors.at(center[0], center[1], max(p[2] for p in points))
        materials[material] += 1
        key = contents, material
        if key not in tag_indices:
            tag_indices[key] = len(tags)
            # Replay shape-tag user data carries native trace surface flags in its low word.
            user_data = ((1 if contents & 1 else 3) << 48) | (material << 19)
            tags.append((contents, 0x1AB7BC33, 0xFFFF, user_data))
        all_contents |= contents
        for i, point in enumerate(points):
            hk.put(vertices + i * 16, "4f", *[(point[k] - center[k]) / 32 for k in range(3)], 0)
            for k in range(3):
                minimum[k] = min(minimum[k], point[k] / 32)
                maximum[k] = max(maximum[k], point[k] / 32)
        hk.put(vertex_array, "QII", vertices, len(points), 16)
        shape = hk.function(0x1E82060, C.c_void_p, C.c_void_p, C.c_float, C.c_void_p)(
            vertex_array, 0, config
        )
        if not shape:
            raise RuntimeError(f"Native convex construction failed for hull {index}")
        hulls.append(shape)
        total_vertices += len(points)
        total_triangles += max(0, len(points) * 2 - 4)
        instance = instances + index * 112
        hk.put(instance, "16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, *[v / 32 for v in center], 1)
        hk.put(instance + 12, "I", 0x3F000040)
        hk.put(instance + 64, "4f", 1, 1, 1, 1)
        hk.put(instance + 80, "QHH", shape, tag_indices[key], 0xFFFF)
        hk.put(instance + 100, "H", 0xFFFF)
        if (index + 1) % 1000 == 0:
            print(f"Built {index + 1}/{len(brushes)} collision hulls", flush=True)
    array = hk.alloc(16)
    hk.put(array, "QII", instances, len(brushes), len(brushes) | 0x80000000)
    world = hk.function(0x161C770, C.c_void_p, C.c_void_p)(array)
    if not world:
        raise RuntimeError("Native world compound construction failed")
    root = hk.alloc(152)

    def field(offset, fmt, values):
        stride = struct.calcsize("<" + fmt)
        memory = hk.alloc(stride * len(values))
        for i, value in enumerate(values):
            hk.put(memory + i * stride, fmt, *value)
        hk.put(root + offset, "QII", memory, len(values), len(values) | 0x80000000)

    name = hk.alloc(32)
    C.memmove(name, b"World Entity Main_Full\0", 23)
    field(0, "Q", [(world,)])
    field(16, "i", [(-1,)])
    field(32, "Q", [(name,)])
    field(48, "I", [(total_vertices,)])
    field(64, "I", [(total_triangles,)])
    field(80, "4f", [(*minimum, 0), (*maximum, 0)])
    field(104, "IIH6xQ", tags)
    field(120, "I", [(all_contents,)])
    field(136, "I", [(len(hulls),)])
    data = hk.save(root, min(256 * 1024 * 1024, 1024 * 1024 + len(hulls) * 8192))
    loaded = hk.load(data)
    restored = hk.pointer(hk.pointer(loaded))
    if (
        C.c_uint.from_address(loaded + 8).value != 1
        or C.c_uint.from_address(restored + 80).value != len(hulls)
        or C.c_uint.from_address(loaded + 112).value != len(tags)
    ):
        raise RuntimeError("Native collision round trip lost shapes or surface tags")
    if C.string_at(hk.pointer(loaded + 104), len(tags) * 24) != C.string_at(
        hk.pointer(root + 104), len(tags) * 24
    ):
        raise RuntimeError("Native collision round trip changed surface tags")
    children = hk.pointer(restored + 72)
    query_bounds = hk.alloc(32)
    maximum_error = 0.0
    for i, shape in enumerate(hulls):
        child = hk.pointer(children + i * 112 + 80)
        if (
            not child
            or C.string_at(child + 24, 24) != C.string_at(shape + 24, 24)
            or C.string_at(children + i * 112, 80)
            != C.string_at(hk.pointer(world + 72) + i * 112, 80)
        ):
            raise RuntimeError(f"Native collision round trip changed hull {i}")
        method = hk.pointer(hk.pointer(child) + 32) - hk.base
        hk.function(method, None, C.c_void_p, C.c_void_p, C.c_void_p)(
            child, children + i * 112, query_bounds
        )
        bounds = list((C.c_float * 8).from_address(query_bounds))
        points = brushes[i][0]
        expected = [min(p[k] for p in points) / 32 for k in range(3)] + [
            max(p[k] for p in points) / 32 for k in range(3)
        ]
        actual = bounds[:3] + bounds[4:7]
        error = max(abs(a - b) for a, b in zip(expected, actual))
        if not all(math.isfinite(v) for v in actual) or error > 0.05:
            raise RuntimeError(f"Native bounds query disagrees with source hull {i}")
        maximum_error = max(maximum_error, error * 32)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    report = dict(
        hulls=len(hulls),
        world_shapes=1,
        bytes=len(data),
        native_roundtrip=True,
        native_bounds_queries=len(hulls),
        maximum_bounds_error=maximum_error,
        surface_tags=len(tags),
        floor_materials=dict(materials),
        sha256=hashlib.sha256(data).hexdigest(),
        source_sha256=hashlib.sha256(collision.read_bytes()).hexdigest(),
    )
    destination.with_suffix(destination.suffix + ".json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--collision", type=Path, required=True)
    parser.add_argument(
        "--footsteps", type=Path, help="Authored floor triangles; default is concrete"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    bake(args.replay, args.collision, args.footsteps, args.output)
