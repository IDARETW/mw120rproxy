"""Create an owned alpha-blended BSP technique using verified Replay state bits."""

import copy
import hashlib
import json
import struct


def create(folder, stem, mapid, kind="glass"):
    if kind not in ("glass", "foliage"):
        raise ValueError("Invalid material kind")
    path = folder / (stem + ".techset.json")
    ts = json.loads(path.read_text())
    material = json.loads((folder / (stem + ".material.json")).read_text())
    # compile_graybox_shader writes a shader dictionary; the converter input
    # is flattened by its existing fromdump reader.
    lit = copy.deepcopy(ts["techniques"][-1])
    header = bytearray.fromhex(lit["header"])
    # ECB646 and ECB564 read GfxStateBits at +A0/+A8. Native reversed-Z:
    # compare table 23E4748 index 3 = GREATER_EQUAL; bit 9 writes depth.
    other = struct.unpack_from("<Q", header, 0xA0)[0]
    other = (other & ~(0xE00 | 3)) | (
        0xC00 if kind == "glass" else 0xE00
    )  # depth test, no depth writes, two-sided
    struct.pack_into("<QQ", header, 0xA0, other, 0x28054 if kind == "glass" else 0)
    # ECF8C0 tables: RGB srcalpha(4), invsrcalpha(5); alpha one(0),
    # invsrcalpha(5); ADD(0). Preserve destination scene alpha convention.
    lit["header"] = header.hex()
    digest = hashlib.sha256(bytes(header) + lit["shaders"][3].encode()).hexdigest()[:12]
    ts["name"] = "tw/mw120r_" + mapid + "_" + kind + "_" + digest
    lit["name"] = "TECHNIQUE_LIT_FORWARDPLUS_BITMASK_mw120r_" + mapid + "_" + kind
    states = bytearray.fromhex(lit["states"])
    for off in range(0, len(states), 16):
        states[off : off + 8] = hashlib.sha256(
            bytes(header) + bytes([off]) + lit["shaders"][3].encode()
        ).digest()[:8]
    lit["states"] = states.hex()
    ts["techniques"] = [lit]
    th = bytearray.fromhex(ts["header"])
    th[24:56] = bytes(32)
    struct.pack_into("<Q", th, 24, 1 << 34)
    ts["header"] = th.hex()
    needed = {f"{14+i}:{n}" for i, n in enumerate(lit["shaders"]) if n}
    ts["shaders"] = {k: v for k, v in ts["shaders"].items() if k in needed}
    definition = stem + "." + kind + ".material.json"
    techfile = stem + "." + kind + ".techset.json"
    material["techset"] = ts["name"]
    material["techsetDefinition"] = techfile
    (folder / definition).write_text(json.dumps(material, indent=2) + "\n")
    (folder / techfile).write_text(json.dumps(ts, indent=2) + "\n")
    return {
        "schema": 1,
        "material": "w/mw120r_" + mapid + "_" + kind,
        "materialDefinition": definition,
    }
