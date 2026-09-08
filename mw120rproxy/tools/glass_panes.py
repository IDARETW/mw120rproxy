"""Preserve independently breakable, planar glass instead of merging all windows."""

import math, struct
from collections import defaultdict


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def unit(v):
    size = math.sqrt(dot(v, v))
    return [x / size for x in v] if size > 1e-6 else None


def hull(points, n):
    # Front/back vertices project to the same point. Choose a plane basis
    # independently of point order, rather than normalizing that zero edge.
    u = unit(cross([0, 0, 1] if abs(n[2]) < 0.9 else [0, 1, 0], n))
    v = cross(n, u)
    p0 = points[0]
    projected = sorted(
        {(round(dot(sub(p, p0), u), 5), round(dot(sub(p, p0), v), 5)) for p in points}
    )

    def turn(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def chain(ps):
        out = []
        for p in ps:
            while len(out) > 1 and turn(out[-2], out[-1], p) <= 1e-5:
                out.pop()
            out.append(p)
        return out

    loop = chain(projected)[:-1] + chain(projected[::-1])[:-1]
    area = abs(sum(a[0] * b[1] - a[1] * b[0] for a, b in zip(loop, loop[1:] + loop[:1]))) / 2
    return [[p0[k] + a * u[k] + b * v[k] for k in range(3)] for a, b in loop], area


def prepare(surfaces, materials):
    result = []
    panes = []
    # A scripted window can comprise several disconnected mesh pieces and
    # materials. Its intact entity is one breakable object, including both sides.
    authored = defaultdict(list)
    remaining = []
    for s in surfaces:
        if (
            "glassGroup" in s
            and "glass" in s["material"].lower()
            and materials[s["material"]].get("blended")
        ):
            authored[s["glassGroup"]].append(s)
        else:
            remaining.append(s)
    for group in authored.values():
        triangles = []
        for s in group:
            for at in range(0, len(s["indices"]), 3):
                p = [s["vertices"][i]["position"] for i in s["indices"][at : at + 3]]
                c = cross(sub(p[1], p[0]), sub(p[2], p[0]))
                triangles.append((dot(c, c), c, p[0]))
        area, c, origin = max(triangles, key=lambda t: t[0])
        n = unit(c)
        if n is None:
            raise ValueError("Degenerate authored glass window")
        if next(x for x in n if abs(x) > 1e-5) < 0:
            n = [-x for x in n]
        points = list({tuple(v["position"]) for s in group for v in s["vertices"]})
        distances = [dot(n, p) for p in points]
        if max(distances) - min(distances) > 16:
            raise ValueError("Authored window is not planar")
        distance = (max(distances) + min(distances)) * 0.5
        projected = [[p[k] + n[k] * (distance - dot(n, p)) for k in range(3)] for p in points]
        polygon, area = hull(projected, n)
        if not 3 <= len(polygon) <= 32 or area < 1:
            raise ValueError("Invalid authored glass polygon")
        pane = len(panes)
        panes.append(
            {
                "normal": n,
                "vertices": polygon,
                "center": [sum(p[k] for p in polygon) / len(polygon) for k in range(3)],
                "area": area,
                "halfThickness": max(0.125, (max(distances) - min(distances)) * 0.5),
            }
        )
        result.extend({**s, "glassPane": pane} for s in group)
    for s in remaining:
        if "glass" not in s["material"].lower() or not materials[s["material"]].get("blended"):
            result.append(s)
            continue
        # Connect triangles by welded position, while keeping different planes
        # separate. OBJ UV seams and hard normals must not divide one pane.
        planes = defaultdict(list)
        for at in range(0, len(s["indices"]), 3):
            tri = s["indices"][at : at + 3]
            p = [s["vertices"][i]["position"] for i in tri]
            n = unit(cross(sub(p[1], p[0]), sub(p[2], p[0])))
            if n is None:
                continue
            if next(x for x in n if abs(x) > 1e-5) < 0:
                n = [-x for x in n]
            key = tuple(round(x, 3) for x in n) + (round(dot(n, p[0]), 1),)
            planes[key].append(tri)
        for key, tris in planes.items():
            parent = list(range(len(tris)))
            owners = {}

            def find(i):
                while parent[i] != i:
                    parent[i] = parent[parent[i]]
                    i = parent[i]
                return i

            for i, tri in enumerate(tris):
                for vi in tri:
                    pos = tuple(round(x, 3) for x in s["vertices"][vi]["position"])
                    if pos in owners:
                        parent[find(i)] = find(owners[pos])
                    else:
                        owners[pos] = i
            groups = defaultdict(list)
            for i, tri in enumerate(tris):
                groups[find(i)].extend(tri)
            for indices in groups.values():
                used = list(dict.fromkeys(indices))
                mapping = {v: i for i, v in enumerate(used)}
                fragment = {
                    **s,
                    "vertices": [s["vertices"][i] for i in used],
                    "indices": [mapping[i] for i in indices],
                }
                points = [v["position"] for v in fragment["vertices"]]
                n = unit(list(key[:3]))
                unique = list({tuple(p) for p in points})
                polygon, area = hull(unique, n)
                if len(polygon) < 3 or len(polygon) > 32 or area < 64:
                    result.append(fragment)
                    continue
                center = [sum(p[k] for p in polygon) / len(polygon) for k in range(3)]
                pane = None
                for j, old in enumerate(panes):
                    # Pair the front/back faces of the same window, but never
                    # join adjacent windows merely because their material matches.
                    if (
                        abs(dot(n, old["normal"])) > 0.995
                        and sum((a - b) ** 2 for a, b in zip(center, old["center"])) < 16
                        and abs(area - old["area"]) < max(1, area * 0.1)
                    ):
                        pane = j
                        break
                if pane is None:
                    pane = len(panes)
                    panes.append({"normal": n, "vertices": polygon, "center": center, "area": area})
                fragment["glassPane"] = pane
                result.append(fragment)
    if len(panes) > 1024:
        raise ValueError("Too many independent glass panes")
    return result, panes


def remove_static_collision(hulls, panes):
    """Replace only thin hulls wholly inside a pane with breakable collision.

    Frames, adjacent walls and larger clip brushes retain their static hulls.
    A spatial bin avoids a hull-by-pane scan on large imported maps.
    """
    bins = defaultdict(list)
    for p in panes:
        lo = [min(v[k] for v in p["vertices"]) - 2 for k in range(3)]
        hi = [max(v[k] for v in p["vertices"]) + 2 for k in range(3)]
        for x in range(math.floor(lo[0] / 128), math.floor(hi[0] / 128) + 1):
            for y in range(math.floor(lo[1] / 128), math.floor(hi[1] / 128) + 1):
                bins[x, y].append(p)
    kept = []
    removed = 0
    for h in hulls:
        points = h["vertices"]
        center = [sum(v[k] for v in points) / len(points) for k in range(3)]
        replace = False
        for p in bins.get((math.floor(center[0] / 128), math.floor(center[1] / 128)), []):
            n = p["normal"]
            polygon = p["vertices"]
            if any(
                abs(dot(sub(v, polygon[0]), n)) > p.get("halfThickness", 0.125) + 0.25
                for v in points
            ):
                continue
            if all(
                all(
                    dot(sub(v, a), cross(n, sub(b, a)))
                    >= -0.25 * math.sqrt(dot(sub(b, a), sub(b, a)))
                    for a, b in zip(polygon, polygon[1:] + polygon[:1])
                )
                for v in points
            ):
                replace = True
                break
        if replace:
            removed += 1
        else:
            kept.append(h)
    return kept, removed


def encode(panes, surfaces):
    refs = defaultdict(list)
    for i, s in enumerate(surfaces):
        if "glassPane" in s:
            refs[s["glassPane"]].append(i)
    out = b"MWRGLS02" + struct.pack("<II", len(panes), len(surfaces))
    for i, pane in enumerate(panes):
        # Transformed meshes can contain almost coincident corners. Quantize
        # first, then weld sub-millimetre edges in the actual serialized polygon.
        # Keep the reader's strict convexity/edge checks unchanged.
        points = [list(struct.unpack("<3f", struct.pack("<3f", *p))) for p in pane["vertices"]]
        while len(points) > 3:
            short = next(
                (
                    j
                    for j, a in enumerate(points)
                    if dot(
                        sub(a, points[(j + 1) % len(points)]), sub(a, points[(j + 1) % len(points)])
                    )
                    < 4e-6
                ),
                None,
            )
            if short is None:
                break
            del points[(short + 1) % len(points)]
        indices = refs[i]
        if not indices:
            raise ValueError("Glass pane lost its render surfaces")
        out += struct.pack(
            "<II3ff", len(points), len(indices), *pane["normal"], pane.get("halfThickness", 0.125)
        )
        out += b"".join(struct.pack("<3f", *p) for p in points)
        out += struct.pack("<" + "I" * len(indices), *indices)
    return out


def validate(data):
    if len(data) < 16 or len(data) > 4 * 1024 * 1024 or data[:8] not in (b"MWRGLS01", b"MWRGLS02"):
        raise ValueError("Invalid glass header")
    count, surfaces = struct.unpack_from("<II", data, 8)
    offset = 16
    used = set()
    if count > 1024 or not 1 <= surfaces <= 4096:
        raise ValueError("Invalid glass count")
    for _ in range(count):
        if offset + 20 > len(data):
            raise ValueError("Truncated glass record")
        nv, ns, *normal = struct.unpack_from("<II3f", data, offset)
        offset += 20
        if data[:8] == b"MWRGLS02":
            if offset + 4 > len(data):
                raise ValueError("Truncated pane thickness")
            thickness = struct.unpack_from("<f", data, offset)[0]
            offset += 4
            if not math.isfinite(thickness) or not 0.125 <= thickness <= 8:
                raise ValueError("Invalid pane thickness")
        if not 3 <= nv <= 32 or not 1 <= ns <= surfaces or offset + 12 * nv + 4 * ns > len(data):
            raise ValueError("Invalid glass polygon")
        vertices = [struct.unpack_from("<3f", data, offset + 12 * i) for i in range(nv)]
        offset += 12 * nv
        refs = struct.unpack_from("<" + "I" * ns, data, offset)
        offset += 4 * ns
        if (
            not all(math.isfinite(v) and abs(v) <= 100000 for p in vertices + [normal] for v in p)
            or abs(dot(normal, normal) - 1) > 0.01
        ):
            raise ValueError("Invalid glass coordinate")
        for ref in refs:
            if ref >= surfaces or ref in used:
                raise ValueError("Duplicate/out of range glass surface")
            used.add(ref)
        area = 0
        for i, a in enumerate(vertices):
            b = vertices[(i + 1) % nv]
            edge = sub(b, a)
            inward = cross(normal, edge)
            if dot(edge, edge) < 1e-6 or any(dot(sub(v, a), inward) < -0.05 for v in vertices):
                raise ValueError("Non-convex glass polygon")
            if abs(dot(sub(a, vertices[0]), normal)) > 0.5:
                raise ValueError("Non-planar glass polygon")
            area += dot(cross(sub(a, vertices[0]), sub(b, vertices[0])), normal)
        if area < 1:
            raise ValueError("Degenerate glass polygon")
    if offset != len(data):
        raise ValueError("Trailing glass data")
    return count
