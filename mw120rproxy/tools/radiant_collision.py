"""Intersect source brush planes and expand prefab transforms into convex hulls."""

from itertools import combinations
import math
import re
import struct
from pathlib import Path
from radiant_source import blocks, properties, vector
from cod4_assets import rotation

SOLID = 0x1
MISSILE_CLIP = 0x80
VEHICLE_CLIP = 0x200
ITEM_CLIP = 0x400
AI_NO_SIGHT = 0x1000
SHOT_CLIP = 0x2000
PLAYER_CLIP = 0x10000
AI_CLIP = 0x20000
SUPPORTED_CONTENTS = (
    SOLID
    | MISSILE_CLIP
    | VEHICLE_CLIP
    | ITEM_CLIP
    | AI_NO_SIGHT
    | SHOT_CLIP
    | PLAYER_CLIP
    | AI_CLIP
)


def contents(hull):
    """Keep collision classes shared by IW3 and Replay; discard compiler-only flags."""
    if "contents" in hull:
        value = hull["contents"]
        if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
            raise ValueError("Invalid source collision contents")
        return value & SUPPORTED_CONTENTS
    tool_contents = {
        "clip": PLAYER_CLIP | AI_CLIP,
        "clip_player": PLAYER_CLIP,
        "clip_monster": AI_CLIP,
        "clip_missile": MISSILE_CLIP,
        "clip_vehicle": VEHICLE_CLIP,
        "clip_item": ITEM_CLIP,
        "clip_shot": SHOT_CLIP,
        "clip_nosight": PLAYER_CLIP | AI_CLIP | AI_NO_SIGHT,
        "clip_nosight_metal": PLAYER_CLIP | AI_CLIP | AI_NO_SIGHT,
    }
    selected = [tool_contents[t] for t in hull.get("textures", []) if t in tool_contents]
    result = 0
    for value in selected:
        result |= value
    return result or SOLID


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def hull(brush, label):
    brush = brush.strip()
    # Curves/terrain are tessellated by cod4map. Their actual compiled collision
    # triangles are imported after the BSP build, not guessed from control points.
    if blocks(brush[1:-1]):
        return None
    rows = re.findall(r"\(\s*([^()]+)\)\s*\(\s*([^()]+)\)\s*\(\s*([^()]+)\)\s*(\S+)", brush)
    if not 4 <= len(rows) <= 64:
        raise ValueError(f"{label}: expected 4..64 brush planes")
    textures = {r[3] for r in rows}
    if textures == {"lightgrid_volume"}:
        return None
    if textures & {"portal", "occluder", "hint", "skip", "trigger", "nonsolid"}:
        raise ValueError(f"{label}: unsupported tool brush {sorted(textures)}")
    planes = []
    for a, b, c, _ in rows:
        a, b, c = map(vector, (a, b, c))
        normal = cross(sub(c, a), sub(b, a))
        length = math.sqrt(dot(normal, normal))
        if length < 1e-8:
            raise ValueError(f"{label}: degenerate plane")
        normal = [x / length for x in normal]
        planes.append((normal, dot(normal, a)))
    points = []
    for (a, da), (b, db), (c, dc) in combinations(planes, 3):
        bc = cross(b, c)
        det = dot(a, bc)
        if abs(det) < 1e-9:
            continue
        ca = cross(c, a)
        ab = cross(a, b)
        p = [(da * bc[k] + db * ca[k] + dc * ab[k]) / det for k in range(3)]
        if all(dot(n, p) <= d + 0.001 for n, d in planes) and not any(
            sum((p[k] - q[k]) ** 2 for k in range(3)) < 1e-8 for q in points
        ):
            points.append(p)
    if not 4 <= len(points) <= 252 or any(abs(x) > 100000 for p in points for x in p):
        raise ValueError(f"{label}: invalid/unbounded convex brush")
    return {"vertices": points, "textures": sorted(textures), "source": label}


def collect(source, map_root):
    dependencies = {}
    result = []

    def visit(path, matrix, origin, ancestry):
        path = path.resolve()
        if not path.is_relative_to(map_root.resolve()):
            raise ValueError(f"Prefab outside map_source: {path}")
        if path in ancestry or len(ancestry) > 16:
            raise ValueError(f"Prefab recursion: {path}")
        text = path.read_text(encoding="utf-8-sig")
        dependencies[str(path)] = text
        entities = blocks(text)
        if not entities or properties(entities[0]).get("classname") != "worldspawn":
            raise ValueError(f"{path}: missing worldspawn")
        transform = lambda p: [dot(row, p) + origin[k] for k, row in enumerate(matrix)]
        for i, brush in enumerate(blocks(entities[0][1:-1])):
            value = hull(brush, f"{path.name}:brush[{i}]")
            if value:
                value["vertices"] = [transform(p) for p in value["vertices"]]
                result.append(value)
        for entity in entities[1:]:
            props = properties(entity)
            if props.get("classname") == "misc_prefab":
                name = props.get("model", "").replace("\\", "/")
                if not name or ".." in name or ":" in name or name.startswith("/"):
                    raise ValueError(f"Invalid prefab path {name}")
                scale = float(props.get("modelscale", "1"))
                if not math.isfinite(scale) or not 0 < scale <= 100:
                    raise ValueError("Invalid prefab scale")
                local = rotation(vector(props.get("angles", "0 0 0")))
                composed = [
                    [sum(matrix[i][k] * local[k][j] for k in range(3)) * scale for j in range(3)]
                    for i in range(3)
                ]
                visit(
                    map_root / name,
                    composed,
                    transform(vector(props.get("origin", "0 0 0"))),
                    ancestry + [path],
                )
            elif blocks(entity[1:-1]):
                raise ValueError(
                    f'{path}: brush entity {props.get("classname")} requires a separate runtime pipeline'
                )

    visit(source, [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0], [])
    if not 1 <= len(result) <= 4096:
        raise ValueError("Expected 1..4096 collision brushes")
    return result, dependencies


def encode(hulls):
    data = b"MWCOLL03" + struct.pack("<I", len(hulls))
    for h in hulls:
        points = h["vertices"]
        value = contents(h)
        if not value:
            raise ValueError("Hull has no supported collision contents")
        data += struct.pack("<II", len(points), value) + b"".join(
            struct.pack("<3f", *p) for p in points
        )
    validate(data)
    return data


def add_compiled_triangles(hulls, geometry, report=None):
    vertices = geometry["vertices"]
    source_contents = geometry.get("triangle_contents")
    if source_contents is not None and len(source_contents) != len(geometry["triangles"]):
        raise ValueError("Collision triangle contents count mismatch")
    added = 0
    skipped_contents = 0
    default_solid = 0
    degenerate = 0
    unreferenced = 0
    for i, tri in enumerate(geometry["triangles"]):
        value = source_contents[i] if source_contents is not None else None
        if source_contents is None:
            value = SOLID
            default_solid += 1
        elif value is None:
            unreferenced += 1
            continue
        collision_contents = contents({"contents": value})
        if not collision_contents:
            skipped_contents += 1
            continue
        points = [vertices[j] for j in tri]
        n = cross(sub(points[1], points[0]), sub(points[2], points[0]))
        length = math.sqrt(dot(n, n))
        if length < 1e-6:
            degenerate += 1
            continue
        # Havok requires a volume, so retain the exact triangle and give it a
        # quarter-unit thickness centered on the compiled collision surface.
        offset = [x / length * 0.125 for x in n]
        hulls.append(
            {
                "vertices": [
                    [p[k] + sign * offset[k] for k in range(3)] for sign in (-1, 1) for p in points
                ],
                "textures": [],
                "contents": collision_contents,
                "source": f"compiled_collision_triangle[{i}]",
            }
        )
        added += 1
    if len(hulls) > 32768:
        raise ValueError("Compiled collision exceeds 32768 hulls; simplify collision geometry")
    if report is not None:
        report.update(
            source_triangles=len(geometry["triangles"]),
            emitted_hulls=added,
            skipped_contents=skipped_contents,
            degenerate=degenerate,
            default_solid=default_solid,
            unreferenced_triangles=unreferenced,
        )
    return added


def validate(data):
    if len(data) < 12 or data[:8] not in (b"MWCOLL01", b"MWCOLL02", b"MWCOLL03"):
        raise ValueError("Bad convex collision header")
    count = struct.unpack_from("<I", data, 8)[0]
    pos = 12
    if not 1 <= count <= 32768:
        raise ValueError("Bad convex hull count")
    if data[:8] == b"MWCOLL01":
        if len(data) != 12 + count * 24:
            raise ValueError("Bad box collision length")
        for row in struct.iter_unpack("<6f", data[12:]):
            if any(not math.isfinite(x) or abs(x) > 100000 for x in row) or any(
                row[k] >= row[k + 3] for k in range(3)
            ):
                raise ValueError("Invalid box collision bounds")
        return count
    typed = data[:8] == b"MWCOLL03"
    for _ in range(count):
        if pos + 4 > len(data):
            raise ValueError("Truncated convex hull count")
        n = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        if typed:
            if pos + 4 > len(data):
                raise ValueError("Truncated collision contents")
            value = struct.unpack_from("<I", data, pos)[0]
            pos += 4
            if not value or value & ~SUPPORTED_CONTENTS:
                raise ValueError("Invalid collision contents")
        if not 4 <= n <= 252 or pos + n * 12 > len(data):
            raise ValueError("Bad convex hull length")
        points = list(struct.iter_unpack("<3f", data[pos : pos + n * 12]))
        pos += n * 12
        if any(not math.isfinite(x) or abs(x) > 100000 for p in points for x in p):
            raise ValueError("Invalid convex hull vertex")
        a = points[0]
        b = max(points, key=lambda p: dot(sub(p, a), sub(p, a)))
        u = sub(b, a)
        c = max(points, key=lambda p: dot(cross(u, sub(p, a)), cross(u, sub(p, a))))
        normal = cross(u, sub(c, a))
        if max(abs(dot(normal, sub(p, a))) for p in points) <= 0.00001:
            raise ValueError("Coplanar convex hull")
    if pos != len(data):
        raise ValueError("Trailing convex collision data")
    return count
