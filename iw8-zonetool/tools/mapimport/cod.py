"""Typed OpenAssetTools IW3/IW4/IW5 interchange, never a guessed raw FF layout."""

import json
import math
from pathlib import Path

from .bsp import align_winding, checked
from .core import (
    Scene,
    add,
    dot,
    entities,
    hull_from_planes,
    mul,
    read_bytes,
    safe_path,
    sha256,
    vec,
)
from .mesh import read_obj


def read_cod(root, source_map=None, expected=None):
    root = Path(root).resolve()
    candidates = sorted(root.glob("maps/**/*.iw8-world.json")) + sorted(
        root.glob("maps/**/*.replay-world.json")
    )
    if source_map:
        candidates = [
            p for p in candidates if p.name.startswith(source_map + ".d3dbsp.")
        ]
    if len(candidates) != 1:
        raise ValueError(
            f"Expected exactly one OAT map world, found {len(candidates)}; use --source-map. IW4/IW5 require tools/install_oat_exporters.py and a rebuilt Unlinker."
        )
    path = candidates[0]
    world = json.loads(read_bytes(path))
    if world.get("schema") != 1:
        raise ValueError("Unsupported OAT world schema")
    engine = world.get(
        "engine", "iw3" if path.name.endswith(".replay-world.json") else None
    )
    if engine not in ("iw3", "iw4", "iw5") or (expected and engine != expected):
        raise ValueError(
            f"OAT engine mismatch: expected {expected}, file declares {engine}"
        )
    scene = Scene(engine)
    scene.dependencies[str(path)] = sha256(path)
    name = world["name"]
    # Validate the name before constructing companion paths.
    stem = safe_path(root, name)
    entity_path = Path(str(stem) + ".ents")
    scene.entities = entities(read_bytes(entity_path).decode("utf-8-sig"))
    scene.dependencies[str(entity_path)] = sha256(entity_path)
    collision_path = Path(
        str(stem)
        + (".replay-collision.json" if engine == "iw3" else ".iw8-collision.json")
    )
    collision = json.loads(read_bytes(collision_path))
    if collision.get("schema") != 1 or collision["name"] != name:
        raise ValueError("OAT collision/world identity mismatch")
    scene.dependencies[str(collision_path)] = sha256(collision_path)
    brush_entities = {
        int(e["model"][1:]): e
        for e in scene.entities
        if e.get("model", "").startswith("*") and e["model"][1:].isdigit()
    }
    models = world.get("brush_models", [])
    selected = list(world["surfaces"])
    if models:
        # Submodel geometry must have an authored visible entity; do not emit
        # trigger volumes or script-only hidden states as opaque architecture.
        selected = list(
            checked(
                world["surfaces"], models[0]["start"], models[0]["count"], "world model"
            )
        )
        for i, m in enumerate(models[1:], 1):
            ent = brush_entities.get(i)
            if not ent or ent.get("classname", "").startswith("trigger"):
                continue
            if (
                ent.get("angles", "0 0 0") != "0 0 0"
                or ent.get("origin", "0 0 0") != "0 0 0"
            ):
                scene.warn(
                    "Transformed CoD brush entities are omitted from the generic importer; use the authored-map pipeline to bake dynamic brush states."
                )
                continue
            selected.extend(
                checked(world["surfaces"], m["start"], m["count"], "brush model")
            )
    for s in selected:
        if not s["indices"]:
            continue
        align_winding(s)
        scene.surfaces.append(s)
    for b in collision["brushes"]:
        if not b["contents"] & (1 | 0x10000):
            continue
        ps = list(b["planes"])
        if "mins" in b:
            for side in range(2):
                for axis in range(3):
                    p = [0.0, 0.0, 0.0, 0.0]
                    p[axis] = 1 if side else -1
                    p[3] = b["maxs"][axis] if side else -b["mins"][axis]
                    ps.append(p)
        scene.hulls.append(hull_from_planes(ps))
    cv = collision.get("vertices", [])
    for tri in collision.get("triangles", []):
        from .core import cross, sub, unit

        if len(tri) != 3 or any(type(i) != int or not 0 <= i < len(cv) for i in tri):
            raise ValueError("Invalid OAT collision indices")
        ps = [vec(cv[i]) for i in tri]
        n = cross(sub(ps[1], ps[0]), sub(ps[2], ps[0]))
        if dot(n, n) < 1e-12:
            continue
        n = mul(unit(n), 0.125)
        scene.hulls.append([add(p, mul(n, k)) for k in (-1, 1) for p in ps])
    cache = {}
    for inst in world.get("models", []):
        model = inst["model"]
        if model not in cache:
            modelpath = safe_path(root, "xmodel/" + model + ".json")
            meta = json.loads(read_bytes(modelpath))
            objpath = safe_path(root, meta["lods"][0]["file"])
            cache[model] = read_obj(objpath, material_libraries=False)
            for warning in cache[model].warnings:
                if not warning.startswith("OBJ contains no gameplay"):
                    scene.warn(warning)
            scene.dependencies[str(modelpath)] = sha256(modelpath)
            scene.dependencies[str(objpath)] = sha256(objpath)
        axis = [vec(row) for row in inst["axis"]]
        origin = vec(inst["origin"])
        scale = float(inst["scale"])
        if not math.isfinite(scale) or scale <= 0:
            raise ValueError("Invalid CoD static model scale")
        # OAT exports OBJ in Y-up coordinates: (x,z,-y). Return to engine space.
        engine_vec = lambda p: [p[0], -p[2], p[1]]
        transform = lambda p, axis=axis: [
            sum(axis[j][i] * p[j] for j in range(3)) for i in range(3)
        ]
        for s in cache[model].surfaces:
            out = {
                "material": s["material"],
                "indices": list(s["indices"]),
                "vertices": [],
            }
            for v in s["vertices"]:
                out["vertices"].append(
                    {
                        "position": add(
                            mul(transform(engine_vec(v["position"])), scale), origin
                        ),
                        "normal": transform(engine_vec(v["normal"])),
                        "uv": v["uv"],
                    }
                )
            align_winding(out)
            scene.surfaces.append(out)
        scene.stats["degenerate_model_faces_removed"] = scene.stats.get(
            "degenerate_model_faces_removed", 0
        ) + cache[model].stats.get("degenerate_faces_removed", 0)
    for name in {s["material"] for s in scene.surfaces}:
        materialpath = safe_path(root, "materials/" + name + ".json")
        if not materialpath.is_file():
            scene.materials[name] = {}
            scene.warn(f"Missing material definition: {name}")
            continue
        m = json.loads(read_bytes(materialpath))
        color = next(
            (
                t.get("image")
                for t in m.get("textures", [])
                if t.get("semantic") in ("colorMap", 2)
            ),
            None,
        )
        scene.materials[name] = {"texture": "images/" + color} if color else {}
        scene.dependencies[str(materialpath)] = sha256(materialpath)
    scene.stats["static_model_instances"] = len(world.get("models", []))
    scene.warn(
        "Generic CoD import retains world geometry, static model LOD0, color textures and compiled collision. Scripts, brush animation, effects, sky and baked lightmaps require separate conversion."
    )
    scene.warn(
        "Compiled CoD collision includes source brush states; verify transformed or scripted brush geometry before gameplay."
    )
    return scene
