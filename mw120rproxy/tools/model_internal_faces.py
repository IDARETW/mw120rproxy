"""Remove coincident internal faces from closed, opaque model meshes."""

from collections import Counter, defaultdict
import copy
import math
from pathlib import Path
import sys


def remove_internal_faces(surface):
    """Clip opposing axis-aligned faces without moving any exterior vertices.

    Open meshes and zero-volume cards are retained. Call only for opaque models:
    a transparent shell can intentionally expose its internal faces.
    """
    vertices, indices = surface["vertices"], surface["indices"]
    triangles = [indices[i : i + 3] for i in range(0, len(indices), 3)]
    edges = Counter()
    volume = 0.0
    groups = defaultdict(list)
    for index, triangle in enumerate(triangles):
        p = [vertices[i]["position"] for i in triangle]
        keys = [tuple(round(c, 5) for c in point) for point in p]
        for a, b in zip(keys, keys[1:] + keys[:1]):
            edges[tuple(sorted((a, b)))] += 1
        a, b, c = p
        volume += (
            sum(
                a[k] * (b[(k + 1) % 3] * c[(k + 2) % 3] - b[(k + 2) % 3] * c[(k + 1) % 3])
                for k in range(3)
            )
            / 6
        )
        for axis in range(3):
            if max(v[axis] for v in p) - min(v[axis] for v in p) > 1e-6:
                continue
            x, y = [k for k in range(3) if k != axis]
            signed_area = (b[x] - a[x]) * (c[y] - a[y]) - (b[y] - a[y]) * (c[x] - a[x])
            if abs(signed_area) > 1e-8:
                groups[(axis, round(a[axis], 5))].append((index, signed_area > 0, p))
            break
    if not triangles or any(count != 2 for count in edges.values()) or abs(volume) < 1e-6:
        return surface, {"changed_triangles": 0, "removed_area": 0.0}

    candidates = [group for group in groups.values() if len({t[1] for t in group}) == 2]
    if not candidates:
        return surface, {"changed_triangles": 0, "removed_area": 0.0}
    vendor = str(Path(__file__).resolve().parent / "_vendor")
    if vendor not in sys.path:
        sys.path.insert(0, vendor)
    import shapely

    replacements = {}
    removed_area = 0.0
    for (axis, _), group in groups.items():
        if len({t[1] for t in group}) != 2:
            continue
        x, y = [k for k in range(3) if k != axis]
        polygons = {i: shapely.Polygon([(v[x], v[y]) for v in p]) for i, _, p in group}
        masks = {
            sign: shapely.union_all([polygons[i] for i, s, _ in group if s == sign])
            for sign in (False, True)
        }
        for index, sign, p in group:
            polygon = polygons[index]
            clipped = polygon.difference(masks[not sign])
            removed = polygon.area - clipped.area
            if removed < 1e-6:
                continue
            removed_area += removed
            replacement = []
            for piece in shapely.get_parts(shapely.constrained_delaunay_triangles(clipped)):
                if piece.area < 1e-9:
                    continue
                a, b, c = p
                determinant = (b[x] - a[x]) * (c[y] - a[y]) - (b[y] - a[y]) * (c[x] - a[x])
                new_triangle = []
                for px, py in list(piece.exterior.coords)[:3]:
                    u = ((px - a[x]) * (c[y] - a[y]) - (py - a[y]) * (c[x] - a[x])) / determinant
                    v = ((b[x] - a[x]) * (py - a[y]) - (b[y] - a[y]) * (px - a[x])) / determinant
                    weights = (1 - u - v, u, v)
                    source = [vertices[i] for i in triangles[index]]
                    vertex = copy.deepcopy(source[0])
                    for field in ("position", "uv", "normal_vec", "tangent_vec"):
                        if field in vertex:
                            vertex[field] = [
                                sum(w * s[field][k] for w, s in zip(weights, source))
                                for k in range(len(vertex[field]))
                            ]
                    length = math.sqrt(sum(c * c for c in vertex["normal_vec"]))
                    vertex["normal_vec"] = [c / length for c in vertex["normal_vec"]]
                    new_triangle.append(vertex)
                q = [vertex["position"] for vertex in new_triangle]
                orientation = (
                    (q[1][x] - q[0][x]) * (q[2][y] - q[0][y])
                    - (q[1][y] - q[0][y]) * (q[2][x] - q[0][x])
                ) > 0
                if orientation != sign:
                    new_triangle.reverse()
                replacement.extend(new_triangle)
            replacements[index] = replacement
    if not replacements:
        return surface, {"changed_triangles": 0, "removed_area": 0.0}
    result = {**surface, "vertices": [], "indices": []}
    for index, triangle in enumerate(triangles):
        result["vertices"].extend(replacements.get(index, [vertices[i] for i in triangle]))
    result["indices"] = list(range(len(result["vertices"])))
    return result, {"changed_triangles": len(replacements), "removed_area": removed_area}
