"""Audit Replay world geometry exported by Atian without trusting unrelated schema fields."""

import argparse
from collections import Counter
import json
from pathlib import Path
import struct


def fields(path):
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    if document.get("profile") != "replay-1.20":
        raise ValueError("Expected a replay-1.20 export")
    return document["asset"]["fields"]


def audit(world, transient):
    surfaces = world["surfaces"]
    records = surfaces["surfaces"]["values"]
    data = surfaces["surfData"]["values"]
    buffers = transient["drawVerts"]
    indices = bytes.fromhex(buffers["indices"]["bytes"])
    issues = []
    if len(records) != surfaces["count"] or len(data) != surfaces["surfDataCount"]:
        issues.append("Surface array extent mismatch")
    if len(indices) != buffers["indexCount"] * 2:
        issues.append("Index buffer extent mismatch")
    for index, surface in enumerate(records):
        tris = surface["tris"]
        data_index = surface["surfDataIndex"]
        if data_index >= len(data):
            issues.append(f"Surface {index}: invalid surfDataIndex")
            continue
        gpu = data[data_index]
        if surface["transientZone"] != transient["transientZoneIndex"]:
            issues.append(f"Surface {index}: needs another transient zone")
            continue
        count = tris["vertexCount"]
        if gpu["xyzOffset"] != tris["posOffset"]:
            issues.append(f"Surface {index}: vertex shader position base mismatch")
        for name, offset, size, limit in (
            ("positions", gpu["xyzOffset"], count * 12, buffers["posDataSize"]),
            ("tangents", gpu["tangentFrameOffset"], count * 4, buffers["auxDataSize"]),
            ("UVs", gpu["texCoordOffset"], count * 8 * gpu["layerCount"], buffers["auxDataSize"]),
            ("lightmap UVs", gpu["lmapCoordOffset"], count * 8, buffers["auxDataSize"]),
        ):
            if offset and (offset % 4 or offset + size > limit):
                issues.append(f"Surface {index}: invalid {name} span")
        start, size = tris["baseIndex"] * 2, tris["triCount"] * 6
        if start + size > len(indices):
            issues.append(f"Surface {index}: invalid index span")
        elif any(i[0] >= count for i in struct.iter_unpack("<H", indices[start : start + size])):
            issues.append(f"Surface {index}: index exceeds local vertex block")
    draw = world["draw"]
    grid = transient["gpuLightGrid"].get("gpuLightGrid")
    return {
        "name": world["name"]["string"],
        "scope": "Single transient zone, Replay surface and GPU buffer spans; not render validation",
        "surfaces": len(records),
        "surface_data_records": len(data),
        "shared_vertex_blocks": sum(
            n > 1 for n in Counter(s["surfDataIndex"] for s in records).values()
        ),
        "ranges": {k: v for k, v in surfaces.items() if k.endswith(("Begin", "End"))},
        "position_bytes": buffers["posDataSize"],
        "auxiliary_bytes": buffers["auxDataSize"],
        "indices": buffers["indexCount"],
        "lights": world["primaryLightCount"],
        "lightmaps": draw["lightmapCount"],
        "reflection_probes": draw["reflectionProbeData"]["reflectionProbeCount"],
        "gpu_light_probes": grid["values"][0]["probeCount"] if grid else 0,
        "umbra_bytes": world["umbraTomeSize"],
        "issues": issues,
        "excluded_schema_fields": [
            "GfxSurfaceBounds: generic export stride does not match Replay's 56 bytes",
            "srfTriangles +4: generic displacement offset label is not established for Replay",
        ],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", type=Path, required=True)
    parser.add_argument("--transient", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = audit(fields(args.world), fields(args.transient))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    raise SystemExit(bool(result["issues"]))
