"""Split source triangle boundaries at existing shared vertices.

IW3 coordinates are quantized to 1/1024 unit. A short edge ending on a
neighbor's unsplit long edge can leave a raster crack after projection. Reuse
that endpoint on both boundaries before packing GPU vertices. This operates on
source world geometry; independently placed props and moving brushes are not
stitched to the world.
"""

from collections import defaultdict
from copy import deepcopy
import math


def _mix(a, b, weight):
    result = deepcopy(a if weight < 0.5 else b)
    for key, value in a.items():
        other = b.get(key)
        if key == "binormal_sign":
            result[key] = value if weight < 0.5 else other
        elif isinstance(value, list) and isinstance(other, list) and len(value) == len(other):
            if all(isinstance(v, (float, int)) for v in value + other):
                values = [x + (y - x) * weight for x, y in zip(value, other)]
                if key in ("color", "vertex_color") and all(isinstance(v, int) for v in value):
                    values = [round(v) for v in values]
                result[key] = values
    return result


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _subtract(a, b):
    return [x - y for x, y in zip(a, b)]


def stitch(surfaces, tolerance=1.0 / 128.0):
    """Append interpolated vertices and replace only triangles with a T-junction.

    Existing vertices, material assignments and triangle winding are retained.
    Only a new boundary vertex may move, by at most ``tolerance``, to coincide
    with an already authored endpoint. Unknown vertex attributes are copied
    from the nearest endpoint; float vector attributes are interpolated.
    """
    if not math.isfinite(tolerance) or not 0 < tolerance <= 1.0 / 128.0:
        raise ValueError("T-junction tolerance must be positive and at most 1/128 unit")
    cell_size = 64.0
    positions = set()
    grid = defaultdict(list)
    for surface in surfaces:
        for vertex in surface["vertices"]:
            position = tuple(vertex["position"])
            if len(position) != 3 or not all(math.isfinite(v) for v in position):
                raise ValueError("Invalid source position")
            positions.add(position)
    for position in sorted(positions):
        grid[tuple(math.floor(v / cell_size) for v in position)].append(position)
    edge_cache = {}
    changed = []
    added_vertices = added_triangles = 0
    max_distance = 0.0

    def intersections(a, b):
        nonlocal max_distance
        key = tuple(sorted((a, b)))
        if key not in edge_cache:
            start, end = key
            delta = _subtract(end, start)
            length_sq = _dot(delta, delta)
            found = []
            if length_sq > tolerance * tolerance * 16:
                length = math.sqrt(length_sq)
                # Sample at no more than half a cell. The neighboring cells
                # include every point within tolerance of this segment.
                steps = max(1, math.ceil(length * 2 / cell_size))
                cells = set()
                for step in range(steps + 1):
                    center = [
                        math.floor((start[i] + delta[i] * step / steps) / cell_size)
                        for i in range(3)
                    ]
                    for x in range(center[0] - 1, center[0] + 2):
                        for y in range(center[1] - 1, center[1] + 2):
                            for z in range(center[2] - 1, center[2] + 2):
                                cells.add((x, y, z))
                for cell in cells:
                    for point in grid.get(cell, ()):
                        relative = _subtract(point, start)
                        weight = _dot(relative, delta) / length_sq
                        if not tolerance / length < weight < 1 - tolerance / length:
                            continue
                        distance = math.sqrt(
                            sum((relative[i] - weight * delta[i]) ** 2 for i in range(3))
                        )
                        if distance <= tolerance:
                            found.append((weight, point, distance))
                found.sort()
                # Multiple attributes can share the same position. Also avoid
                # creating microscopic segments from nearby duplicate endpoints.
                clean = []
                for hit in found:
                    if clean and (hit[0] - clean[-1][0]) * length <= tolerance:
                        if hit[2] < clean[-1][2]:
                            clean[-1] = hit
                    else:
                        clean.append(hit)
                found = clean
            edge_cache[key] = found
        found = edge_cache[key]
        if a == key[0]:
            return found
        return [(1 - weight, point, distance) for weight, point, distance in reversed(found)]

    for surface_index, surface in enumerate(surfaces):
        vertices = surface["vertices"]
        original_count = len(vertices)
        output = []
        vertex_cache = {}
        split_count = 0
        for offset in range(0, len(surface["indices"]), 3):
            triangle = surface["indices"][offset : offset + 3]
            if len(triangle) != 3:
                raise ValueError("Source index count is not divisible by three")
            boundary = []
            for i, j in zip(triangle, triangle[1:] + triangle[:1]):
                boundary.append(i)
                a, b = vertices[i], vertices[j]
                for weight, point, distance in intersections(
                    tuple(a["position"]), tuple(b["position"])
                ):
                    cache_key = (min(i, j), max(i, j), point)
                    if cache_key not in vertex_cache:
                        vertex = _mix(a, b, weight)
                        vertex["position"] = list(point)
                        vertex_cache[cache_key] = len(vertices)
                        vertices.append(vertex)
                    boundary.append(vertex_cache[cache_key])
                    max_distance = max(max_distance, distance)
            if len(boundary) == 3:
                output.extend(triangle)
                continue
            center = _mix(
                _mix(vertices[triangle[0]], vertices[triangle[1]], 0.5),
                vertices[triangle[2]],
                1.0 / 3.0,
            )
            center_index = len(vertices)
            vertices.append(center)
            for a, b in zip(boundary, boundary[1:] + boundary[:1]):
                output.extend((a, b, center_index))
            split_count += 1
        if split_count:
            added_vertices += len(vertices) - original_count
            added_triangles += (len(output) - len(surface["indices"])) // 3
            changed.append(
                {
                    "surface": surface_index,
                    "material": surface.get("material"),
                    "split_triangles": split_count,
                }
            )
            surface["indices"] = output
    return {
        "tolerance": tolerance,
        "changed_surfaces": changed,
        "added_vertices": added_vertices,
        "added_triangles": added_triangles,
        "maximum_edge_adjustment": max_distance,
    }
