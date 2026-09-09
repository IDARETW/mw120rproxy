"""Validated, engine-neutral geometry. Coordinates are right handed and Z-up."""

import hashlib
import json
import math
import re
import struct
from dataclasses import dataclass, field
from itertools import combinations
from pathlib import Path

MAX_FILE = 512 * 1024 * 1024
MAX_VERTICES = 2_000_000
MAX_TRIANGLES = 1_000_000


def read_bytes(path):
    path = Path(path)
    if path.stat().st_size > MAX_FILE:
        raise ValueError(f"Input exceeds 512 MiB: {path}")
    return path.read_bytes()


def safe_path(root, name):
    name = str(name).replace("\\", "/")
    if (
        not name
        or ":" in name
        or name.startswith("/")
        or any(p in ("..", "") for p in name.split("/"))
    ):
        raise ValueError(f"Unsafe asset path: {name!r}")
    path = (Path(root) / name).resolve()
    if not path.is_relative_to(Path(root).resolve()):
        raise ValueError(f"Asset escapes its source directory: {name!r}")
    return path


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(
        json.dumps(value, allow_nan=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    temp.replace(path)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def vec(value, size=3):
    if isinstance(value, str):
        value = value.split()
    result = list(map(float, value))
    if len(result) != size or any(not math.isfinite(x) for x in result):
        raise ValueError(f"Expected {size} finite components: {value!r}")
    return result


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def mul(a, b):
    return [x * b for x in a]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]


def unit(a):
    length = math.sqrt(dot(a, a))
    if length < 1e-10:
        raise ValueError("Degenerate normal or triangle")
    return mul(a, 1 / length)


def entities(text):
    # Strict lexer: reject malformed tails instead of regex-skipping source data.
    token = re.compile(r'\s+|//[^\n]*|/\*.*?\*/|[{}]|"(?:\\.|[^"\\])*"', re.DOTALL)
    items, pos = [], 0
    text = text.rstrip("\0")
    while pos < len(text):
        match = token.match(text, pos)
        if not match:
            raise ValueError(f"Malformed entity text at byte {pos}")
        t = match.group()
        pos = match.end()
        if not t.isspace() and not t.startswith(("//", "/*")):
            items.append(t)
    output, i = [], 0
    while i < len(items):
        if items[i] != "{":
            raise ValueError("Expected entity opening brace")
        i += 1
        ent = {}
        while i < len(items) and items[i] != "}":
            if i + 1 >= len(items) or not all(
                t.startswith('"') for t in items[i : i + 2]
            ):
                raise ValueError("Expected quoted entity key/value")
            unquote = lambda t: t[1:-1].replace('\\"', '"').replace("\\\\", "\\")
            key, value = map(unquote, items[i : i + 2])
            if key in ent:
                raise ValueError(f"Duplicate entity key: {key}")
            ent[key] = value
            i += 2
        if i == len(items):
            raise ValueError("Unclosed entity")
        output.append(ent)
        i += 1
    return output


def hull_from_planes(planes):
    if not 4 <= len(planes) <= 64:
        raise ValueError("Convex brush must have 4..64 planes")
    normalized = []
    for p in planes:
        p = vec(p, 4)
        length = math.sqrt(dot(p[:3], p[:3]))
        if length < 1e-8:
            raise ValueError("Zero brush plane")
        n, d = mul(p[:3], 1 / length), p[3] / length
        if not any(
            max(abs(n[k] - q[k]) for k in range(3)) < 1e-8 and abs(d - e) < 1e-6
            for q, e in normalized
        ):
            normalized.append((n, d))
    # Most map solids are axial. Avoid cubic plane intersections for thousands
    # of boxes, while keeping clipped and slanted hulls on the exact path below.
    bounds = [[-math.inf, math.inf] for _ in range(3)]
    axial = True
    for n, d in normalized:
        axes = [k for k in range(3) if abs(n[k]) > 1e-8]
        if len(axes) != 1:
            axial = False
            break
        k = axes[0]
        if n[k] > 0:
            bounds[k][1] = min(bounds[k][1], d / n[k])
        else:
            bounds[k][0] = max(bounds[k][0], d / n[k])
    if axial:
        if any(not all(math.isfinite(x) for x in b) or b[0] >= b[1] for b in bounds):
            raise ValueError("Empty, degenerate or unbounded convex brush")
        from itertools import product

        return [list(p) for p in product(*bounds)]
    points = []
    for (a, da), (b, db), (c, dc) in combinations(normalized, 3):
        bc = cross(b, c)
        det = dot(a, bc)
        if abs(det) < 1e-9:
            continue
        p = mul(
            add(add(mul(bc, da), mul(cross(c, a), db)), mul(cross(a, b), dc)), 1 / det
        )
        if all(dot(n, p) <= d + 0.002 for n, d in normalized) and not any(
            dot(sub(p, q), sub(p, q)) < 1e-6 for q in points
        ):
            points.append(p)
    if not 4 <= len(points) <= 252:
        raise ValueError("Empty, degenerate or unbounded convex brush")
    return points


def triangulate(points):
    """Ear clipping preserves concave polygons; a fan would fill holes incorrectly."""
    if len(points) < 3 or len(points) > 8192:
        raise ValueError("Invalid polygon size")
    normal = [0.0, 0.0, 0.0]
    for p, q in zip(points, points[1:] + points[:1]):
        normal = add(normal, cross(p, q))
    axis = max(range(3), key=lambda i: abs(normal[i]))
    if abs(normal[axis]) < 1e-8:
        raise ValueError("Degenerate polygon")
    xy = [[p[k] for k in range(3) if k != axis] for p in points]
    area = sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(xy, xy[1:] + xy[:1]))
    sign = 1 if area > 0 else -1
    turn = lambda a, b, c: (
        sign * ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]))
    )
    remaining, out = list(range(len(points))), []
    while len(remaining) > 3:
        for j, b in enumerate(remaining):
            a, c = remaining[j - 1], remaining[(j + 1) % len(remaining)]
            if turn(xy[a], xy[b], xy[c]) <= 1e-9:
                continue
            if any(
                all(
                    v >= -1e-9
                    for v in (
                        turn(xy[a], xy[b], xy[k]),
                        turn(xy[b], xy[c], xy[k]),
                        turn(xy[c], xy[a], xy[k]),
                    )
                )
                for k in remaining
                if k not in (a, b, c)
            ):
                continue
            out.extend((a, b, c))
            remaining.pop(j)
            break
        else:
            raise ValueError("Self-intersecting or degenerate polygon")
    out.extend(remaining)
    return out


@dataclass
class Scene:
    engine: str
    surfaces: list = field(default_factory=list)
    hulls: list = field(default_factory=list)
    entities: list = field(default_factory=list)
    materials: dict = field(default_factory=dict)
    warnings: list = field(default_factory=list)
    dependencies: dict = field(default_factory=dict)
    stats: dict = field(default_factory=dict)

    def warn(self, text):
        if text not in self.warnings:
            self.warnings.append(text)

    def validate(self):
        nv, nt = 0, 0
        for surface in self.surfaces:
            vertices, indices = surface["vertices"], surface["indices"]
            if len(vertices) < 3 or not indices or len(indices) % 3:
                raise ValueError("Empty or incomplete source mesh")
            nv += len(vertices)
            nt += len(indices) // 3
            for v in vertices:
                v["position"] = vec(v["position"])
                if any(abs(x) > 100000 for x in v["position"]):
                    raise ValueError(
                        "Geometry exceeds Replay coordinate limits; use --scale"
                    )
                v["uv"] = vec(v.get("uv", [0, 0]), 2)
                v["normal"] = unit(vec(v["normal"]))
            if any(type(i) is not int or i < 0 or i >= len(vertices) for i in indices):
                raise ValueError("Triangle index outside vertex buffer")
        if not nt or nv > MAX_VERTICES or nt > MAX_TRIANGLES:
            raise ValueError("Source mesh is empty or exceeds import limits")
        for hull in self.hulls:
            if not 4 <= len(hull) <= 252:
                raise ValueError("Invalid convex hull size")
            for p in hull:
                if any(abs(x) > 100000 for x in vec(p)):
                    raise ValueError("Collision exceeds coordinate limits")
        self.stats.update(
            surfaces=len(self.surfaces),
            vertices=nv,
            triangles=nt,
            collision_hulls=len(self.hulls),
        )

    def transform(self, scale=1.0, up="z"):
        if not math.isfinite(scale) or scale <= 0:
            raise ValueError("Scale must be finite and positive")
        rotate = lambda p: [p[0], -p[2], p[1]] if up == "y" else list(p)
        for s in self.surfaces:
            for v in s["vertices"]:
                v["position"] = mul(rotate(v["position"]), scale)
                v["normal"] = rotate(v["normal"])
        self.hulls = [[mul(rotate(p), scale) for p in h] for h in self.hulls]
        for e in self.entities:
            if "origin" in e:
                e["origin"] = " ".join(map(str, mul(rotate(vec(e["origin"])), scale)))

    def triangle_collision(self):
        for s in self.surfaces:
            if s.get("nonsolid"):
                continue
            for i in range(0, len(s["indices"]), 3):
                p = [s["vertices"][j]["position"] for j in s["indices"][i : i + 3]]
                n = cross(sub(p[1], p[0]), sub(p[2], p[0]))
                if dot(n, n) < 1e-12:
                    continue
                offset = mul(unit(n), 0.125)
                self.hulls.append([add(q, mul(offset, k)) for k in (-1, 1) for q in p])
        self.warn(
            "Collision for mesh geometry uses 0.25-unit triangular prisms; source physics and navigation are not converted."
        )


def encode_collision(hulls):
    if not 1 <= len(hulls) <= 32768:
        raise ValueError(
            "Replay collision requires 1..32768 hulls; use simplified collision geometry"
        )
    out = bytearray(b"MWCOLL02" + struct.pack("<I", len(hulls)))
    for points in hulls:
        if not 4 <= len(points) <= 252:
            raise ValueError("Invalid hull size")
        a = points[0]
        b = max(points, key=lambda p: dot(sub(p, a), sub(p, a)))
        c = max(
            points,
            key=lambda p: dot(cross(sub(b, a), sub(p, a)), cross(sub(b, a), sub(p, a))),
        )
        n = cross(sub(b, a), sub(c, a))
        if max(abs(dot(n, sub(p, a))) for p in points) <= 0.00001:
            raise ValueError("Coplanar collision hull")
        out.extend(struct.pack("<I", len(points)))
        for p in points:
            p = vec(p)
            if any(abs(x) > 100000 for x in p):
                raise ValueError("Collision vertex outside Replay bounds")
            out.extend(struct.pack("<3f", *p))
    return out
