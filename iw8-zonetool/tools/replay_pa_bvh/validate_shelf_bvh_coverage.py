"""Compare ray-hit shelf triangles with their serialized Replay 1.20 BVH leaves.

Accepts a Replay asset JSON (whose ``asset.fields.havokData.bytes`` contains
TAG0) or a raw TAG0 dump. The fixed offsets below are for the TAG0 layout emitted
by the Replay 1.20 Havok serializer; no converter code or live process is used.

The check is deliberately focused on triangles intersected by the captured
bullet segment. For every such triangle it resolves the SIMD-tree leaf key,
checks that leaf's six-float AABB encloses all decoded packet vertices within
the supplied tolerance, and checks whether the ray enters the saved AABB.
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path


DEFAULT_OLD = Path(
    r"E:\mw124-conversion-builds\pa-shelf-bvh-2\mw19replay\srv_mp_4doffice"
    r"\assets\physicsasset\mw120r\mp_4doffice\smodel_7.0.asset.json"
)
DEFAULT_NEW = Path(r"D:\mw124\_static_mesh_probe\bake_shelf_fixed.bin")

# The recorded ray in the packaged Replay PhysicsMesh basis. The raw OBJ probe
# uses original OBJ axes, so it needs the inverse model-axis rotation instead.
RAYS = {
    "replay-shape": ((1.60696673, 1.91511309, 1.81640625),
                     (-29.64213943, -35.01847839, -16.97380638)),
    "raw-obj": ((1.60696673, 1.81640625, -1.91511309),
                (-29.64213943, -16.97380638, 35.01847839)),
}

# Replay 1.20 TAG0 data offsets, confirmed against its type-info arrays.
GLOBAL_MIN = 0x550
GLOBAL_MAX = 0x560
SECTION_MIN = 0x570
SECTION_SCALE = 0x57C
SHARED_REMAP = 0x640
PRIMITIVES = 0x5A0
PACKED_VERTICES = 0x680
SHARED_VERTICES = 0x6F0
SIMD_NODES = 0x800
SIMD_NODE_STRIDE = 0x80
SIMD_NODE_COUNT = 16  # Retained as the known old-build count; read from ITEM below.


def find_sections(data: bytes):
    """Return TAG0 sections using their encoded total section sizes."""
    sections = {}
    offset = 8
    while offset + 8 <= len(data):
        encoded_size = struct.unpack_from(">I", data, offset)[0]
        size = encoded_size & 0x0FFFFFFF
        tag = data[offset + 4:offset + 8]
        if size < 8 or offset + size > len(data):
            raise ValueError(f"invalid {tag!r} section size {size} at 0x{offset:x}")
        sections.setdefault(tag, []).append((offset, size))
        offset += size
    if offset != len(data):
        raise ValueError(f"trailing bytes after TAG0 sections at 0x{offset:x}")
    return sections


def find_items(data: bytes):
    sections = find_sections(data)
    if b"DATA" not in sections or b"INDX" not in sections:
        raise ValueError("TAG0 is missing DATA or INDX section")
    data_start, data_size = sections[b"DATA"][0]
    index_start, index_size = sections[b"INDX"][0]
    index_end = index_start + index_size
    item_start = None
    offset = index_start + 8
    while offset + 8 <= index_end:
        section_size = struct.unpack_from(">I", data, offset)[0] & 0x0FFFFFFF
        tag = data[offset + 4:offset + 8]
        if section_size < 8 or offset + section_size > index_end:
            break  # Nested INDX payload contains non-section data after ITEM.
        if tag == b"ITEM":
            item_start = offset
            break
        offset += section_size
    if item_start is None:
        # ITEM is the first subsection in the INDX payload for these TAG0 assets.
        item_start = index_start + 8
        if data[item_start + 4:item_start + 8] != b"ITEM":
            raise ValueError("could not locate ITEM table")
    item_bytes = struct.unpack_from(">I", data, item_start)[0] & 0x0FFFFFFF
    records = []
    for i in range((item_bytes - 8) // 12):
        type_flags, relative_offset, count = struct.unpack_from("<III", data, item_start + 8 + 12 * i)
        records.append((type_flags & 0x00FFFFFF, data_start + 8 + relative_offset, count))
    return records


def get_tag0(path: Path) -> bytes:
    if path.suffix.lower() == ".json":
        value = json.loads(path.read_text(encoding="utf-8"))
        try:
            data = bytes.fromhex(value["asset"]["fields"]["havokData"]["bytes"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError(f"{path}: missing PhysicsAsset havokData bytes") from exc
    else:
        data = path.read_bytes()

    if len(data) < 8 or data[4:8] != b"TAG0":
        raise ValueError(f"{path}: input is not a TAG0 stream")
    declared_size = struct.unpack_from(">I", data, 0)[0]
    if declared_size != len(data):
        raise ValueError(
            f"{path}: TAG0 header declares {declared_size} bytes, got {len(data)}"
        )
    return data


def decode_geometry(data: bytes):
    items = find_items(data)
    # Resolve arrays from the serializer's type-info table. Geometry encoding
    # stays fixed to the Replay version, while array offsets/counts are taken
    # from ITEM so node count changes do not silently truncate traversal.
    by_type = {}
    for type_id, absolute_offset, count in items:
        by_type.setdefault(type_id, []).append((absolute_offset, count))
    def one(type_id, count=None):
        choices = [item for item in by_type.get(type_id, []) if count is None or item[1] == count]
        if len(choices) != 1:
            raise ValueError(f"ITEM type {type_id} expected one array, found {choices}")
        return choices[0]

    prim_offset, prim_count = one(550)
    node_offset, node_count = one(285)
    if prim_count != 40 or node_offset != SIMD_NODES:
        raise ValueError(f"unexpected Replay tree layout: primitives={prim_count}, node offset=0x{node_offset:x}")
    # Other arrays are currently stable in this TAG0 version; check ITEM proves
    # the primitive/node arrays and node count for each input artifact.
    node_count = node_count
    def floats(offset):
        return struct.unpack_from("<3f", data, offset)

    global_min = floats(GLOBAL_MIN)
    global_max = floats(GLOBAL_MAX)
    section_min = floats(SECTION_MIN)
    section_scale = floats(SECTION_SCALE)

    vertices = []
    for index in range(60):
        if index < 28:
            bits = struct.unpack_from("<I", data, PACKED_VERTICES + 4 * index)[0]
            q = (bits & 0x7FF, (bits >> 11) & 0x7FF, (bits >> 22) & 0x3FF)
            point = tuple(section_min[a] + section_scale[a] * q[a] for a in range(3))
        else:
            remap = struct.unpack_from("<H", data, SHARED_REMAP + 2 * (index - 28))[0]
            bits = struct.unpack_from("<Q", data, SHARED_VERTICES + 8 * remap)[0]
            q = (bits & 0x1FFFFF, (bits >> 21) & 0x1FFFFF, (bits >> 42) & 0x3FFFFF)
            point = tuple(
                global_min[a]
                + (global_max[a] - global_min[a])
                * q[a]
                / ((1 << (21 if a < 2 else 22)) - 1)
                for a in range(3)
            )
        vertices.append(point)

    packets = [
        tuple(data[prim_offset + 4 * index : prim_offset + 4 * index + 4])
        for index in range(prim_count)
    ]

    def bounds_for(node, lane):
        base = SIMD_NODES + node * SIMD_NODE_STRIDE
        vectors = [struct.unpack_from("<4f", data, base + 16 * axis) for axis in range(6)]
        return tuple(vector[lane] for vector in vectors)

    def child(node, lane):
        return struct.unpack_from("<I", data, SIMD_NODES + node * SIMD_NODE_STRIDE + 96 + lane * 4)[0]

    def node_union(node):
        bounds = [bounds_for(node, lane) for lane in range(4) if child(node, lane) != 0xFFFFFFFF]
        if not bounds:
            return None
        return tuple((min if axis % 2 == 0 else max)(b[axis] for b in bounds) for axis in range(6))

    def contains(outer, inner, tol=0.001):
        return all(outer[2 * axis] - tol <= inner[2 * axis]
                   and outer[2 * axis + 1] + tol >= inner[2 * axis + 1]
                   for axis in range(3))

    # Child IDs that fall in [1,node_count) can resemble primitive keys. Treat
    # them as internal only when the parent lane encloses that child's union.
    leaves = {}
    internal = []
    for node in range(1, node_count):
        for lane in range(4):
            key = child(node, lane)
            if key == 0xFFFFFFFF:
                continue
            bounds = bounds_for(node, lane)
            is_internal = (1 <= key < node_count and key > node
                           and node_union(key) is not None
                           and contains(bounds, node_union(key)))
            if is_internal:
                internal.append((node, lane, key))
            else:
                leaves.setdefault(key, []).append((node, lane, bounds))
    return vertices, packets, leaves, node_count, internal


def subtract(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def intersect_triangle(vertices, triangle, origin, delta):
    a, b, c = (vertices[index] for index in triangle)
    edge1, edge2 = subtract(b, a), subtract(c, a)
    pvec = cross(delta, edge2)
    determinant = dot(edge1, pvec)
    if abs(determinant) < 1e-9:
        return None
    inverse = 1.0 / determinant
    tvec = subtract(origin, a)
    u = dot(tvec, pvec) * inverse
    qvec = cross(tvec, edge1)
    v = dot(delta, qvec) * inverse
    t = dot(edge2, qvec) * inverse
    if 0.0 <= t <= 1.0 and u >= 0.0 and v >= 0.0 and u + v <= 1.0:
        return t, u, v
    return None


def ray_intersects_bounds(bounds, origin, delta):
    low, high = 0.0, 1.0
    for axis in range(3):
        minimum, maximum = bounds[2 * axis], bounds[2 * axis + 1]
        direction = delta[axis]
        if abs(direction) < 1e-12:
            if origin[axis] < minimum or origin[axis] > maximum:
                return False
            continue
        t0 = (minimum - origin[axis]) / direction
        t1 = (maximum - origin[axis]) / direction
        low = max(low, min(t0, t1))
        high = min(high, max(t0, t1))
        if low > high:
            return False
    return True


def check_asset(path: Path, tolerance: float, ray_basis: str):
    data = get_tag0(path)
    vertices, packets, leaves, node_count, internal = decode_geometry(data)
    if ray_basis == "auto":
        # Infer the coordinate convention from the decoded model extents, not
        # the container type: both old and new assets may be raw TAG0 or JSON.
        extent = tuple(struct.unpack_from("<f", data, GLOBAL_MAX + 4 * a)[0]
                       - struct.unpack_from("<f", data, GLOBAL_MIN + 4 * a)[0]
                       for a in range(3))
        y_long = extent[1] > extent[2] * 1.35
        z_long = extent[2] > extent[1] * 1.35
        if y_long == z_long:
            raise ValueError(f"{path}: cannot infer ray basis from global extents {extent}")
        ray_basis = "replay-shape" if y_long else "raw-obj"
    origin, delta = RAYS[ray_basis]
    results = []

    for packet_index, packet in enumerate(packets):
        if any(index >= len(vertices) for index in packet):
            continue  # Native 0xDEAD packet sentinel.
        triangles = [packet[:3]]
        if packet[3] != packet[2]:
            # Replay invokes the callback once per packed packet, and the same
            # even packet key covers both triangles in the quad.
            triangles.append((packet[0], packet[2], packet[3]))

        for triangle_index, triangle in enumerate(triangles):
            intersection = intersect_triangle(vertices, triangle, origin, delta)
            if intersection is None:
                continue
            key = packet_index * 2
            candidates = leaves.get(key, [])
            if not candidates:
                results.append({
                    "packet": packet_index,
                    "triangle": triangle_index,
                    "key": hex(key),
                    "t": intersection[0],
                    "leaf_found": False,
                    "encloses_vertices": False,
                    "ray_enters_leaf": False,
                })
                continue

            # A primitive key is expected to occur once; preserve all matches in
            # the report if malformed data duplicates it.
            for node, lane, bounds in candidates:
                encloses = all(
                    bounds[2 * axis] - tolerance <= vertices[index][axis]
                    <= bounds[2 * axis + 1] + tolerance
                    for index in triangle
                    for axis in range(3)
                )
                results.append({
                    "packet": packet_index,
                    "triangle": triangle_index,
                    "key": hex(key),
                    "node": node,
                    "lane": lane,
                    "t": intersection[0],
                    "bounds": [round(value, 8) for value in bounds],
                    "encloses_vertices": encloses,
                    "ray_enters_leaf": ray_intersects_bounds(bounds, origin, delta),
                })

    return {
        "path": str(path),
        "tag0_bytes": len(data),
        "ray_basis": ray_basis,
        "node_count": node_count,
        "internal_links": len(internal),
        "leaf_entries": sum(len(entries) for entries in leaves.values()),
        "ray_hit_triangles": len(results),
        "captured_segment_hits_geometry": bool(results),
        "all_hit_triangles_have_enclosing_leaves": all(
            row.get("leaf_found", True) and row["encloses_vertices"] for row in results
        ),
        "all_hit_leaves_intersect_ray": all(row["ray_enters_leaf"] for row in results),
        "hits": results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", type=Path, default=DEFAULT_OLD, help="previous packaged JSON/TAG0")
    parser.add_argument("--new", type=Path, default=DEFAULT_NEW, help="corrected raw TAG0 dump")
    parser.add_argument("--old-ray-basis", choices=("auto", *RAYS), default="auto",
                        help="ray coordinate basis; auto infers it from the serialized model extents")
    parser.add_argument("--new-ray-basis", choices=("auto", *RAYS), default="auto",
                        help="ray coordinate basis; auto infers it from the serialized model extents")
    parser.add_argument("--tolerance", type=float, default=0.001,
                        help="vertex/AABB tolerance in model units (default: 0.001)")
    args = parser.parse_args()
    if args.tolerance < 0 or not math.isfinite(args.tolerance):
        parser.error("--tolerance must be a finite nonnegative value")

    old = check_asset(args.old, args.tolerance, args.old_ray_basis)
    new = check_asset(args.new, args.tolerance, args.new_ray_basis)
    print(json.dumps({"old": old, "corrected": new}, indent=2))

    # The regression signature is old leaves excluding the hit geometry and
    # corrected leaves enclosing it while admitting the same segment.
    # The pre-fix asset can fail by omitting the ray-hit side geometry entirely;
    # report that zero-hit case as the old failure signature.
    old_failed = (
        not old["captured_segment_hits_geometry"]
        or not old["all_hit_triangles_have_enclosing_leaves"]
        or not old["all_hit_leaves_intersect_ray"]
    )
    new_passed = (
        new["all_hit_triangles_have_enclosing_leaves"]
        and new["all_hit_leaves_intersect_ray"]
    )
    if not old_failed or not new_passed:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
