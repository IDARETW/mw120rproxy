"""Create glass, foliage, and sky BSP techniques using Replay's native state bits."""

import copy
import hashlib
import json
import struct


def create(folder, stem, mapid, kind="glass"):
    if kind not in ("glass", "foliage", "sky"):
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
    coverage = kind == "foliage" and ts.get("coveragePrepass", False)
    other = (other & ~(0xE00 | 3)) | (
        (0x800 if coverage else 0xE00) if kind == "foliage" else 0xC00
    )
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
    techniques = [lit]
    if coverage:
        depth = copy.deepcopy(ts["techniques"][0])
        dh = bytearray.fromhex(depth["header"])
        struct.pack_into("<Q", dh, 0xA0, struct.unpack_from("<Q", dh, 0xA0)[0] & ~3)
        depth["header"] = dh.hex()
        states = bytearray.fromhex(depth["states"])
        for off in range(0, len(states), 16):
            states[off : off + 8] = hashlib.sha256(
                bytes(dh) + bytes([off]) + depth["shaders"][3].encode()
            ).digest()[:8]
        depth["states"] = states.hex()
        techniques.insert(0, depth)
        if ts.get("coverageShadows", False):
            for source in ts["techniques"]:
                sh = bytearray.fromhex(source["header"])
                if struct.unpack_from("<I", sh, 8)[0] not in (27, 28):
                    continue
                shadow = copy.deepcopy(source)
                # Leaf cards are two-sided, including their cast silhouette.
                # Keep all native shadow bias and depth bits intact.
                struct.pack_into("<Q", sh, 0xA0, struct.unpack_from("<Q", sh, 0xA0)[0] & ~3)
                shadow["header"] = sh.hex()
                states = bytearray.fromhex(shadow["states"])
                for off in range(0, len(states), 16):
                    states[off : off + 8] = hashlib.sha256(
                        bytes(sh) + bytes([off]) + shadow["shaders"][3].encode()
                    ).digest()[:8]
                shadow["states"] = states.hex()
                techniques.insert(-1, shadow)
    ts["techniques"] = techniques
    th = bytearray.fromhex(ts["header"])
    # Replay's material classifiers read these flags independently of PSO state.
    # Shipped tw/lit_*_alt techsets use 0x80 for alpha testing; bit 0 is two-sided.
    flags = struct.unpack_from("<Q", th, 8)[0]
    flags &= ~(0x8080 | 1)
    if kind == "foliage":
        flags |= 0xA1
    elif kind == "glass":
        flags = (flags & ~0x20) | 1
    struct.pack_into("<Q", th, 8, flags)
    th[24:56] = bytes(32)
    mask = sum(1 << struct.unpack_from("<I", bytes.fromhex(t["header"]), 8)[0] for t in techniques)
    struct.pack_into("<Q", th, 24, mask)
    ts["header"] = th.hex()
    needed = {f"{14+i}:{n}" for t in techniques for i, n in enumerate(t["shaders"]) if n}
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
