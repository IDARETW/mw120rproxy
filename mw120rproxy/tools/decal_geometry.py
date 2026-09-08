"""Preserve decal separation when baking CoD4 surfaces into a shared BSP material."""

from replay_mesh_math import unpack_normal


def separate(surfaces, materials):
    # Quantization is only used to match source triangles, never to rewrite them.
    # Three matching corners are required; proximity to a wall is not sufficient.
    def key(s, tri):
        return tuple(sorted(tuple(round(x, 3) for x in s["vertices"][i]["position"]) for i in tri))

    def overlay(m):
        return m.get("cutout") or m.get("alpha_test") or m.get("blended")

    solid = {}
    for s in surfaces:
        m = materials[s["material"]]
        if overlay(m) or m.get("depth_offset"):
            continue
        for a in range(0, len(s["indices"]), 3):
            tri = s["indices"][a : a + 3]
            solid[key(s, tri)] = unpack_normal(s["vertices"][tri[0]]["normal"])
    authored = matched = vertices = 0
    for s in surfaces:
        m = materials[s["material"]]
        offset = m.get("depth_offset", 0.0)
        if offset:
            used = set(s["indices"])
            authored += len(s["indices"]) // 3
        elif overlay(m) and "glass" not in s["material"].lower():
            used = set()
            for a in range(0, len(s["indices"]), 3):
                tri = s["indices"][a : a + 3]
                normal = solid.get(key(s, tri))
                if normal is None:
                    continue
                n = unpack_normal(s["vertices"][tri[0]]["normal"])
                if sum(x * y for x, y in zip(n, normal)) > 0.9:
                    used.update(tri)
                    matched += 1
            offset = 0.0625
        else:
            continue
        for i in used:
            v = s["vertices"][i]
            n = unpack_normal(v["normal"])
            s["vertices"][i] = {**v, "position": [p + offset * d for p, d in zip(v["position"], n)]}
            vertices += 1
    return {
        "authored_offset_triangles": authored,
        "coplanar_overlay_triangles": matched,
        "offset_vertices": vertices,
    }
