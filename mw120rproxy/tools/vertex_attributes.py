"""Preserve authored RGBA vertex colors without turning opaque materials translucent."""

import math


def color(vertex):
    value = vertex.get("color", [255, 255, 255, 255])
    if len(value) != 4 or any(
        not isinstance(x, (int, float)) or not math.isfinite(x) or x != int(x) or not 0 <= x <= 255
        for x in value
    ):
        raise ValueError("Vertex color must contain four RGBA bytes")
    return [int(x) for x in value]


def coverage_flags(material):
    """Bits 3..4 retain the authored alpha-test comparator; zero keeps older packages valid."""
    if not material.get("cutout"):
        return 0
    return {"ge128": 0, "gt0": 8, "lt128": 16}.get(material.get("alpha_test_mode"), 0)


def atlas_vertices(vertices, tile, flags, lightmaps):
    """Encode each draw's vertices without modifying shared glass-fragment inputs."""
    from map_lighting import coordinates

    output = []
    for source in vertices:
        vertex = dict(source)
        index = vertex.pop("baked_index", 255)
        uv = vertex.pop("baked_uv", [0, 0])
        baked = 0 <= index < len(lightmaps)
        coord = coordinates(lightmaps[index], uv) if baked else [0, 0]
        vertex["lightmapUV"] = [
            tile + 0.25 + coord[0] * 0.25,
            flags + (4 if baked else 0) + 0.25 + coord[1] * 0.25,
        ]
        output.append(vertex)
    return output
