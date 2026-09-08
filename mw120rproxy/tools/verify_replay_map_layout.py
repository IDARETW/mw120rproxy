"""Walk the minimal map package using sizes read from the exact Replay executable.

This checks disk consumption and reservations, not rendering or Havok validity.
Native evidence: Load_MapEnts E0B520, Load_GfxWorld D92E80, pointer loaders
D96DA0/DB2EB0/DA6CD0, FixStreamAlignment D8D480, Load_Stream 11B2A20.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import re
import pefile


def verify(game, package, map_id):
    if not re.fullmatch(r"mp_[a-z0-9_]{1,60}", map_id):
        raise ValueError("Invalid map id")
    owned_material = ("w/mw120r_" + map_id).encode("ascii")
    glass_material = owned_material + b"_glass"
    foliage_material = owned_material + b"_foliage"

    def owned_techset(name):
        return (
            re.fullmatch(
                b"tw/mw120r_" + map_id.encode("ascii") + b"_(?:(?:glass|foliage)_)?[0-9a-f]{12}",
                name,
            )
            is not None
        )

    from radiant_source import validate_collision

    manifest = json.loads((package / "manifest.json").read_text())
    if manifest.get("ladders") or (package / "ladders.bin").exists():
        from ladder_data import validate as validate_ladders

        if manifest.get("ladders") not in ("faces-v1", "faces-v2"):
            raise ValueError("Unsupported ladder manifest")
        validate_ladders((package / "ladders.bin").read_bytes())
    if manifest.get("glass") or (package / "glass.bin").exists():
        from glass_panes import validate as validate_glass

        if manifest.get("glass") not in ("panes-v1", "panes-v2"):
            raise ValueError("Unsupported glass manifest")
        validate_glass((package / "glass.bin").read_bytes())
    if manifest.get("collision") not in (None, "boxes-v1", "convex-v2"):
        raise ValueError("Unsupported collision manifest")
    if manifest.get("collision") or (package / "collision.bin").exists():
        data = (package / "collision.bin").read_bytes()
        if manifest.get("collision") == "convex-v2":
            from radiant_collision import validate

            validate(data)
        else:
            validate_collision(data)
    exe = game.read_bytes()
    if hashlib.md5(exe).hexdigest() != "1c238fe327f2ecc3b0db924c5b425439":
        raise ValueError("Wrong Replay executable")
    pe = pefile.PE(data=exe, fast_load=True)
    sizes = {
        t: struct.unpack("<I", pe.get_data(0x2458160 + 4 * t, 4))[0]
        for t in (11, 14, 17, 18, 19, 23, 24, 25, 29, 31, 61)
    }
    if sizes != {
        11: 120,
        14: 40,
        17: 40,
        18: 64,
        19: 232,
        23: 0xF8,
        24: 0xA8,
        25: 0x10,
        29: 0x408,
        31: 0x4590,
        61: 0x20,
    }:
        raise ValueError(f"Unexpected native asset sizes: {sizes}")
    tags = []
    for i in range(41):
        ptr = struct.unpack("<Q", pe.get_data(0x4598378 + i * 24, 8))[0]
        tags.append(pe.get_data(ptr - 0x140000000, 32).split(b"\0")[0].decode("ascii"))
    header = Path(__file__).resolve().parents[3] / "iw8-zonetool/src/iw8/replay_netconst.h"
    if re.findall(r'"([a-z0-9]+)"', header.read_text()) != tags:
        raise ValueError("Converter NCS type/tag table differs from Replay")
    expected_name = f"maps/mp/{map_id}.d3dbsp".encode()
    report = {"native_asset_sizes": sizes, "zones": [], "game_launched": False}

    for prefix, types in (
        ("srv_", [29, 23, 24] + [61] * 41),
        ("", [31, 25]),
        ("eng_", []),
        ("ww_", []),
        ("techsets_", []),
    ):
        path = package / f"{prefix}{map_id}.ff"
        data = path.read_bytes()
        if data[:8] != b"IWffc100" or data[0x88:0x8C] != b"\x01IWC":
            raise ValueError(f"{path.name}: expected unsigned stored Replay file")
        body, pos = data[0x8C:], 0
        blocks = struct.unpack_from("<11Q", data, 0x30)
        virtual = 0

        def take(n):
            nonlocal pos
            if n < 0 or pos + n > len(body):
                raise ValueError(f"{path.name}: truncated at {pos:#x}, need {n:#x}")
            result = body[pos : pos + n]
            pos += n
            return result

        def name(expected=expected_name):
            nonlocal pos, virtual
            end = body.find(b"\0", pos)
            if end < 0 or body[pos:end] != expected:
                raise ValueError(
                    f"{path.name}: wrong asset string at {pos:#x}; layout is desynchronized"
                )
            virtual += end + 1 - pos
            pos = end + 1

        def q(buf, off):
            return struct.unpack_from("<Q", buf, off)[0]

        def u(buf, off):
            return struct.unpack_from("<I", buf, off)[0]

        def vtake(n, mask):
            nonlocal virtual
            virtual = ((virtual + mask) & ~mask) + n
            return take(n)

        def read_material(material, allow_definition=False):
            if q(material, 0) != 2**64 - 2:
                raise ValueError("Missing native material name")
            end = body.index(b"\0", pos)
            reference = body[pos:end]
            if reference not in (
                b",$default",
                b",mo/white_3d",
                b",w/mw120r_test",
                b"w/mw120r_test",
                owned_material,
                b"," + owned_material,
                glass_material,
                b"," + glass_material,
                foliage_material,
                b"," + foliage_material,
            ):
                raise ValueError("Unrecognized material reference")
            name(reference)
            if reference.startswith(b","):
                if any(material[8:]):
                    raise ValueError("Reference must contain only its name")
            else:
                if (
                    not allow_definition
                    or reference
                    not in (b"w/mw120r_test", owned_material, glass_material, foliage_material)
                    or material[28:32] != bytes([3, 4, 1, 0])
                ):
                    raise ValueError("Unexpected custom world material counts")
                if any(material[40:64]) or q(material, 0x58) or q(material, 0x70):
                    raise ValueError("Runtime material fields must not be serialized")
                for off in (0x40, 0x48, 0x50, 0x60, 0x68):
                    if q(material, off) != 2**64 - 2:
                        raise ValueError("Missing material array")
                techset = take(64)
                if q(techset, 0) != 2**64 - 2 or any(techset[8:]):
                    raise ValueError("Invalid techset reference")
                ts_name = body[pos : body.index(b"\0", pos)]
                if ts_name not in (
                    b",w/lit_3_lit_rpl_ta1_804000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0",
                    b",w/mw120r_graybox_v1",
                    b",tw/mw120r_graybox_v1",
                ) and not (ts_name.startswith(b",") and owned_techset(ts_name[1:])):
                    raise ValueError("Unexpected world technique reference")
                name(ts_name)
                textures = vtake(48, 7)
                for i in range(3):
                    if q(textures, i * 16 + 8) != 2**64 - 2:
                        raise ValueError("Missing image reference")
                    im = take(0xE8)
                    if q(im, 0) != 2**64 - 2 or any(im[8:]):
                        raise ValueError("Invalid neutral image")
                    image = body[pos : body.index(b"\0", pos)]
                    if image not in (
                        b",$gray",
                        b",$identitynormalmap",
                        b",$black",
                    ) and image not in {b"," + x for x in image_names}:
                        raise ValueError("Missing ordered material image dependency")
                    name(image)
                vtake(80, 15)
                indices = vtake(195, 0)
                if any(x not in (0, 255) for x in indices):
                    raise ValueError("Constant buffer index outside table")
                cb = vtake(0x110, 15)
                if any(cb[0x30:]):
                    raise ValueError("Serialized GPU constant buffer handles")
                for k in range(4):
                    length = u(cb, 4 * k)
                    if length > 65536 or q(cb, 16 + 8 * k) != (2**64 - 2 if length else 0):
                        raise ValueError("Invalid shader constant stage")
                    if length:
                        vtake(length, 15)
            return reference

        def mapents():
            nonlocal virtual
            me = take(sizes[29])
            if q(me, 0) != 2**64 - 2 or q(me, 8) != 2**64 - 2:
                raise ValueError("MapEnts string tags differ")
            if u(me, 0x148) or q(me, 0x150) or u(me, 0x158):
                raise ValueError("This verifier expects the empty-physics prototype")
            name()
            ents = take(u(me, 0x10))
            virtual += len(ents)
            if not ents.endswith(b"\0") or b"worldspawn" not in ents:
                raise ValueError("Missing complete MapEnts entity string")
            if manifest.get("collision") in ("boxes-v1", "convex-v2"):
                from replay_entity_validation import validate_baseline_anchor

                validate_baseline_anchor(ents)
            count = struct.unpack_from("<H", me, 0x128)[0]
            if count:
                if q(me, 0x130) != 2**64 - 2 or u(me, 0x128) != count:
                    raise ValueError("Invalid compiled spawn-list framing")
                records = vtake(count * 40, 3)
                expected = []
                for ent in re.findall(rb"\{([^{}]*)\}", ents):
                    fields = dict(re.findall(rb'(\d+)\s+"([^"]*)"', ent))
                    classname = fields.get(b"212", b"")
                    if classname.startswith(b"mp_") and b"_spawn" in classname:
                        expected.append(fields)
                if len(expected) != count:
                    raise ValueError("Compiled spawns do not cover entity source")
                for i, fields in enumerate(expected):
                    index, pad, n, t, s, *vectors = struct.unpack_from("<HHIII6f", records, i * 40)
                    if (
                        index != i
                        or pad
                        or min(n, t, s) == 0
                        or max(n, t, s) >= len(script_strings)
                    ):
                        raise ValueError("Invalid compiled spawn record or script-string index")
                    if [script_strings[x] for x in (n, t, s)] != [
                        fields.get(k, b"") for k in (b"212", b"1070", b"845")
                    ]:
                        raise ValueError("Spawn script-string references differ from source")
                    expected_vec = [
                        float(v) for k in (b"709", b"80") for v in fields.get(k, b"0 0 0").split()
                    ]
                    # Native records contain float32. At SM64's large world
                    # coordinates, one float32 ULP exceeds the old 0.0001 limit.
                    expected_vec = struct.unpack("<6f", struct.pack("<6f", *expected_vec))
                    if tuple(vectors) != expected_vec:
                        raise ValueError("Compiled spawn position or angles differ from source")
            for off in (0x1C8, 0x1D0):
                if q(me, off):
                    population = vtake(56, 7)
                    if q(me, off) != 2**64 - 2:
                        raise ValueError("Invalid population pointer")
                    if any(population):
                        if (
                            q(population, 0) != 2**64 - 2
                            or q(population, 8) != 2**64 - 2
                            or u(population, 0x28) != 1
                            or u(population, 0x2C)
                        ):
                            raise ValueError("Invalid empty dynamic-entity population")
                        tree = vtake(24, 7)
                        if q(tree, 0) != 2**64 - 2 or q(tree, 8) != 2**64 - 2 or u(tree, 16) != 1:
                            raise ValueError("Missing dynamic-entity spatial leaf tree")
                        if any(vtake(8, 3)):
                            raise ValueError("Expected unsplit leaf")
                        if struct.unpack("<6f", vtake(24, 3)) != (-100000.0,) * 3 + (100000.0,) * 3:
                            raise ValueError("Invalid spatial partition extents")
                        if vtake(4, 3) != b"\xff" * 4:
                            raise ValueError("Missing empty spatial bucket sentinel")
            if q(me, 0x1C8) and u(me, 0x1E8) != 0xFFFFFFFF:
                raise ValueError("Missing empty non-spatial entity sentinel")
            if q(me, 0x2C0):
                tree = vtake(24, 7)
                if (
                    q(me, 0x2C0) != 2**64 - 2
                    or q(tree, 0) != 2**64 - 2
                    or q(tree, 8) != 2**64 - 2
                    or u(tree, 16) != 1
                ):
                    raise ValueError("Invalid scriptable spatial tree")
                if (
                    any(vtake(8, 3))
                    or struct.unpack("<6f", vtake(24, 3)) != (-100000.0,) * 3 + (100000.0,) * 3
                ):
                    raise ValueError("Invalid scriptable spatial leaf")

        root = take(32)
        if prefix == "" and u(root, 16) == 3:
            types = [11, 31, 25]
        if prefix == "" and u(root, 16) == 12:
            types = [14] * 4 + [17] * 4 + [18, 11, 31, 25]
        if prefix == "" and u(root, 16) == 10:
            types = [14] * 3 + [17] * 3 + [18, 11, 31, 25]
        image_candidate = prefix == "" and 10 <= u(root, 16) <= 18
        if image_candidate:
            types = [0] * u(root, 16)
        if u(root, 16) != len(types) or u(root, 0) > 65536:
            raise ValueError("Unexpected asset or script-string counts")
        script_strings = []
        if u(root, 0):
            if q(root, 8) != 2**64 - 2:
                raise ValueError("Missing inline script-string list")
            pointers = struct.unpack(f"<{u(root,0)}Q", vtake(u(root, 0) * 8, 7))
            for i, pointer in enumerate(pointers):
                if not pointer:
                    script_strings.append(b"")
                    continue
                if pointer != 2**64 - 2:
                    raise ValueError("Invalid script-string pointer")
                end = body.find(b"\0", pos)
                if end < 0:
                    raise ValueError("Unterminated script string")
                value = body[pos:end]
                script_strings.append(value)
                name(value)
            if script_strings[0] != b"":
                raise ValueError("Missing null script-string index zero")
        header_bytes = vtake(len(types) * 16, 7)
        headers = [header_bytes[i * 16 : (i + 1) * 16] for i in range(len(types))]
        if image_candidate:
            types = [u(h, 0) for h in headers]
            image_count = types.count(19)
            if types not in (
                [19] * image_count + [14] * 3 + [17] * 3 + [18, 11, 31, 25],
                [19] * image_count + [14] * 3 + [17] * 3 + [18, 11, 18, 11, 31, 25],
                [19] * image_count + [14] * 3 + [17] * 3 + [18, 11, 18, 11, 18, 11, 31, 25],
                [14] * 4 + [17] * 4 + [18, 11, 31, 25],
            ):
                raise ValueError("Unexpected render asset dependency order")
        if [u(h, 0) for h in headers] != types or any(q(h, 8) != 2**64 - 3 for h in headers):
            raise ValueError("Unexpected asset array")
        ncs_index = 0
        shader_names = set()
        image_names = set()
        for asset_type in types:
            # Native DB_InsertPointer reserves a slot before each top-level body.
            virtual = ((virtual + 7) & ~7) + 8
            if asset_type == 29:
                mapents()
                continue
            asset = take(sizes[asset_type])
            if q(asset, 0) != 2**64 - 2:
                raise ValueError("Missing inline asset-name tag")
            if asset_type == 11:
                read_material(asset, True)
                continue
            if asset_type == 19:
                sn = body[pos : body.index(b"\0", pos)]
                if not sn.startswith(b"mw120r/") or len(sn) > 128 or sn in image_names:
                    raise ValueError("Invalid resident image name")
                name(sn)
                width, height, depth, elements = struct.unpack_from("<4H", asset, 0x24)
                if not 1 <= width <= 4096 or not 1 <= height <= 4096 or depth != 1 or elements != 1:
                    raise ValueError("Invalid resident image dimensions")
                expected = bytearray(232)
                levels = asset[0x30]
                if not 1 <= levels <= max(width, height).bit_length():
                    raise ValueError("Invalid resident mip count")
                length = sum(max(1, width >> i) * max(1, height >> i) * 4 for i in range(levels))
                struct.pack_into("<Q", expected, 0, 2**64 - 2)
                struct.pack_into("<III", expected, 0x14, 7, 1 if levels > 1 else 3, length)
                struct.pack_into("<4H", expected, 0x24, width, height, 1, 1)
                expected[0x2E:0x31] = bytes([1, 1, levels])
                struct.pack_into("<Q", expected, 0xE0, 2**64 - 2)
                if asset != expected:
                    raise ValueError(
                        "Invalid resident image metadata or serialized runtime pointer"
                    )
                # Native E2FFF0 consumes these bytes from stream 1, not virtual.
                pixels = take(length)
                if blocks[1] < len(pixels) + 232:
                    raise ValueError("Resident image exceeds temporary stream reservation")
                image_names.add(sn)
                continue
            if asset_type in (14, 17):
                sn = body[pos : body.index(b"\0", pos)]
                name(sn)
                if len(sn) > 128 or not sn:
                    raise ValueError("Invalid shader name")
                shader_names.add((asset_type, sn))
                if q(asset, 8):
                    if q(asset, 8) != 2**64 - 2:
                        raise ValueError("Invalid debug name tag")
                    name(body[pos : body.index(b"\0", pos)])
                length = u(asset, 32)
                if q(asset, 16) or length > 65536 or q(asset, 24) != (2**64 - 2 if length else 0):
                    raise ValueError("Invalid serialized shader program")
                if length and not vtake(length, 3).startswith(b"DXBC"):
                    raise ValueError("Shader program is not DXBC")
                continue
            if asset_type == 18:
                ts_name = body[pos : body.index(b"\0", pos)]
                if ts_name not in (
                    b"w/lit_3_lit_rpl_ta1_804000_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0",
                    b"w/mw120r_graybox_v1",
                    b"tw/mw120r_graybox_v1",
                ) and not owned_techset(ts_name):
                    raise ValueError("Unexpected world technique definition")
                name(ts_name)
                if (
                    q(asset, 24) not in (0x418000009, 0x418000001, 1 << 34)
                    or any(asset[32:56])
                    or q(asset, 56) != 2**64 - 2
                ):
                    raise ValueError("Unexpected world technique mask")
                count = q(asset, 24).bit_count()
                if vtake(count * 8, 7) != struct.pack(
                    "<" + str(count) + "Q", *([2**64 - 2] * count)
                ):
                    raise ValueError("Missing technique pointers")
                for expected_type in (
                    [34] if count == 1 else [0, 27, 28, 34] if count == 4 else [0, 3, 27, 28, 34]
                ):
                    te = vtake(184, 7)
                    if q(te, 0) != 2**64 - 2 or u(te, 8) != expected_type or any(te[0x30:0x50]):
                        raise ValueError("Invalid technique header/runtime pointers")
                    if count == 4 and te[0x9C] not in (32, 34, 35):
                        raise ValueError("Static BSP has incompatible shader layout")
                    if count == 1 and (
                        te[0x9C] != 35
                        or (q(te, 0xA0), q(te, 0xA8))
                        != ((0xE00, 0) if b"_foliage_" in ts_name else (0xC00, 0x28054))
                        or not any(k in ts_name for k in (b"_foliage_", b"_glass_"))
                    ):
                        raise ValueError("Invalid transparent BSP blend/depth state")
                    tn = body[pos : body.index(b"\0", pos)]
                    name(tn)
                    if not tn.startswith(b"TECHNIQUE_"):
                        raise ValueError("Invalid technique name")
                    for off in (0x28, 0x50, 0x88, 0xB0):
                        if q(te, off) != 2**64 - 2:
                            raise ValueError("Missing technique array")
                    states = vtake(te[0x83] * te[0xE] * 16, 7)
                    if any(any(states[o : o + 8]) for o in range(8, len(states), 16)):
                        raise ValueError("Serialized pipeline handle")
                    vtake(16, 0)
                    for typ, off in ((14, 0x58), (15, 0x60), (16, 0x68), (17, 0x70)):
                        if not q(te, off):
                            continue
                        if q(te, off) != 2**64 - 2:
                            raise ValueError("Invalid shader pointer tag")
                        sh = take(40)
                        if q(sh, 0) != 2**64 - 2 or any(sh[8:]):
                            raise ValueError("Shader reference has data")
                        sn = body[pos : body.index(b"\0", pos)]
                        name(sn)
                        if not sn.startswith(b",") or (typ, sn[1:]) not in shader_names:
                            raise ValueError("Missing ordered shader dependency")
                    vtake(te[0x83] * 5, 0)
                    vtake(sum(te[0x78:0x7C]) * 6, 1)
                continue
            if asset_type == 61:
                name(f"ncs_{tags[ncs_index]}_level".encode())
                if (
                    u(asset, 8) != ncs_index
                    or u(asset, 12) != 2
                    or u(asset, 16)
                    or u(asset, 20)
                    or q(asset, 24)
                ):
                    raise ValueError("Wrong NCS type/source or nonempty prototype table")
                ncs_index += 1
                continue
            name()
            if asset_type == 23:
                if q(asset, 0x18) != 2**64 - 2 or u(asset, 0xB8) or q(asset, 0xC0):
                    raise ValueError("Unexpected prototype clipMap linkage/physics")
                mapents()
                if q(asset, 0x20):
                    if q(asset, 0x20) != 2**64 - 2 or asset[0x28] != 1:
                        raise ValueError("Wrong default stage count")
                    stage = vtake(40, 7)
                    if (
                        q(stage, 0) != 2**64 - 2
                        or stage[0x16] not in (0, 1)
                        or any(stage[8:0x16] + stage[0x17:])
                    ):
                        raise ValueError("Invalid default stage")
                    name(b"default")
            elif asset_type == 25:
                if q(asset, 8) != 2**64 - 2 or take(40) != bytes(40):
                    raise ValueError("Missing valid empty G_GlassData object")
                virtual = ((virtual + 7) & ~7) + 40
            elif asset_type == 24:
                lights = u(asset, 0x30)
                if lights not in (1, 2) or q(asset, 0x38) != 2**64 - 2 or take(160) != bytes(160):
                    raise ValueError("Missing reserved ComPrimaryLight zero")
                virtual = ((virtual + 7) & ~7) + 160 * lights
                if lights == 2:
                    sun = take(160)
                    direction = struct.unpack_from("<3f", sun, 0x2C)
                    if (
                        sun[1] != 1
                        or q(sun, 0x98)
                        or struct.unpack_from("<f", sun, 0x10)[0] <= 0
                        or abs(sum(x * x for x in direction) - 1) > 1e-5
                        or any(x <= 0 for x in struct.unpack_from("<3f", sun, 0x20))
                    ):
                        raise ValueError("Invalid authored sun light")
            elif asset_type == 31:
                name()
                if (
                    u(asset, 0x18) not in (1, 2)
                    or u(asset, 0x14) != u(asset, 0x18) - 1
                    or q(asset, 0x3D68) != 2**64 - 2
                    or blocks[4] < u(asset, 0x18) * 152 + 4
                ):
                    raise ValueError("Missing reserved runtime GfxLight zero")
                if q(asset, 0xB0) != 2**64 - 2 or blocks[4] < 2204:
                    raise ValueError("Missing scene entity cell visibility storage")
                if (
                    u(asset, 0x3FA0) != 1
                    or q(asset, 0x41C0) != 2**64 - 2
                    or q(asset, 0x3EF8) != 2**64 - 2
                ):
                    raise ValueError("Missing primary light or sun-lit cell visibility storage")
                if u(asset, 0x3F20) != 160 or q(asset, 0x3F28) != 2**64 - 2:
                    raise ValueError("Missing per-client entity motion storage")
                if (
                    u(asset, 0x90) != 1
                    or struct.unpack_from("<H", asset, 0xA0)[0] != 1
                    or q(asset, 0xA8) != 2**64 - 2
                    or q(asset, 0xB8) != 2**64 - 2
                    or q(asset, 0x3EF0) != 2**64 - 2
                    or q(asset, 0x3F30)
                ):
                    raise ValueError("Replay GfxWorld cell/visibility layout differs")
                if take(2) != b"\x01\0":
                    raise ValueError("Wrong single-leaf DPVS node")
                virtual = ((virtual + 1) & ~1) + 2
                if q(asset, 0xC0) != 2**64 - 2 or take(4) != bytes(4):
                    raise ValueError("Missing resident-cell transient mapping")
                virtual = ((virtual + 1) & ~1) + 4
                # Native FixStreamAlignment moves memory only, so no disk padding.
                cell = take(40)
                virtual = ((virtual + 7) & ~7) + 40
                if u(cell, 0x18) or q(cell, 0x20):
                    raise ValueError("Unexpected cell portals")
                count = u(asset, 0xC8)
                if count > 4096:
                    raise ValueError("Excessive mesh surface count")
                words = (count + 31) // 32
                surface_records = []
                gpu_records = []
                if count:
                    if u(asset, 0xCC) != count or u(asset, 0x3F9C) != words:
                        raise ValueError("Surface/data/visibility counts disagree")
                    if any(
                        q(asset, o) != 2**64 - 2 for o in (0xF0, 0xF8, 0x100, 0x108, 0x41E0, 0x41F8)
                    ):
                        raise ValueError("Missing surface arrays")
                    raw = vtake(count * 40, 7)
                    surface_records = [raw[i * 40 : (i + 1) * 40] for i in range(count)]
                    material_refs = []
                    for sf in surface_records:
                        if q(sf, 16) != 2**64 - 2:
                            raise ValueError("Missing surface material")
                        material_refs.append(read_material(take(120)))
                    transparent = sum(
                        r in (b"," + glass_material, b"," + foliage_material) for r in material_refs
                    )
                    opaque = count - transparent
                    if any(
                        r in (b"," + glass_material, b"," + foliage_material)
                        for r in material_refs[:opaque]
                    ) or any(
                        r not in (b"," + glass_material, b"," + foliage_material)
                        for r in material_refs[opaque:]
                    ):
                        raise ValueError("Cutout/glass must follow opaque surfaces")
                    if [u(asset, o) for o in range(0xD0, 0xF0, 4)] != [
                        0,
                        opaque,
                        opaque,
                        opaque,
                        opaque,
                        count,
                        count,
                        count,
                    ]:
                        raise ValueError("Native surface sort ranges disagree with materials")
                    vtake(count * 56, 3)
                    if any(vtake(count * 16, 7)):
                        raise ValueError("Expected runtime-sorted draw surfaces")
                    raw = vtake(count * 88, 63)
                    gpu_records = [raw[i * 88 : (i + 1) * 88] for i in range(count)]
                    if blocks[2] < sizes[31] + count * 120 or blocks[4] < 0x4000:
                        raise ValueError("Missing material/visibility reservations")
                if u(asset, 0x7CC) != 1 or q(asset, 0x7D0) != 2**64 - 2:
                    raise ValueError("Missing resident render zone zero")
                if q(asset, 0x738):
                    ies = take(0xE8)
                    if q(asset, 0x738) != 2**64 - 2 or q(ies, 0) != 2**64 - 2 or any(ies[8:]):
                        raise ValueError("Invalid neutral IES image reference")
                    name(b",$white")
                transient = take(0x148)
                if (
                    q(transient, 0) != 2**64 - 2
                    or u(transient, 0xD8) != 1
                    or q(transient, 0xE0) != 2**64 - 2
                    or q(transient, 0xE8) != 2**64 - 2
                ):
                    raise ValueError("Unexpected resident render zone")
                name()
                if count:
                    pos_size, aux_size, index_count = (
                        u(transient, 0x10),
                        u(transient, 0x14),
                        u(transient, 0xA8),
                    )
                    if any(q(transient, o) != 2**64 - 2 for o in (0x18, 0x20, 0xB0)):
                        raise ValueError("Missing geometry buffers")
                    positions = vtake(pos_size, 3)
                    aux = vtake(aux_size, 3)
                    indices = vtake(index_count * 2, 3)
                    for i, (sf, gpu) in enumerate(zip(surface_records, gpu_records)):
                        verts, tris = struct.unpack_from("<HH", sf, 8)
                        po, base_index = u(sf, 0), u(sf, 12)
                        if (
                            not verts
                            or not tris
                            or po + verts * 12 > len(positions)
                            or base_index + tris * 3 > index_count
                        ):
                            raise ValueError("Surface geometry exceeds buffers")
                        if u(sf, 24) != i or u(gpu, 8) != po:
                            raise ValueError("CPU/GPU surface references disagree")
                        ix = struct.unpack_from(f"<{tris*3}H", indices, base_index * 2)
                        if max(ix) >= verts:
                            raise ValueError("Triangle index exceeds vertex count")
                        for off, stride in ((12, 4), (16, 8), (24, 8)):
                            if u(gpu, off) + verts * stride > len(aux):
                                raise ValueError("Auxiliary attribute exceeds buffer")
                if (
                    take(4) != b"\x01\0\0\0"
                    or take(8) != b"\xfe" + b"\xff" * 7
                    or take(4) != b"\x01\0\0\0"
                ):
                    raise ValueError("Resident cell tree framing differs")
                virtual = ((virtual + 3) & ~3) + 4
                virtual = ((virtual + 7) & ~7) + 8
                tree = take(48)
                virtual = ((virtual + 7) & ~7) + 48
                if u(tree, 24) != count or any(tree[28:]):
                    raise ValueError("Cell tree does not cover mesh surfaces")
                preload_size = ((sizes[31] + 31) & ~31) + 32
                model = take(96)
                if (
                    u(asset, 0x3DF0) != 1
                    or q(asset, 0x3DF8) != 2**64 - 2
                    or u(model, 0x58) != count
                    or any(model[:0x58])
                    or any(model[0x5C:])
                ):
                    raise ValueError("Missing world brush-model zero")
                virtual = ((virtual + 3) & ~3) + 96
                if count:
                    sorted_surfaces = struct.unpack(f"<{words*32}I", vtake(words * 32 * 4, 3))
                    if sorted_surfaces != tuple(i if i < count else 0 for i in range(words * 32)):
                        raise ValueError("Invalid sorted surface table")
                if blocks[1] < sizes[31] or blocks[2] < preload_size or blocks[4] < 4:
                    raise ValueError("Missing Load/Preload/Postload or visibility reservation")
        if pos != len(body):
            raise ValueError(f"{path.name}: {len(body)-pos} unconsumed disk bytes")
        if blocks[5] < virtual:
            raise ValueError(
                f"{path.name}: VIRTUAL reservation {blocks[5]} < native requirement {virtual}"
            )
        report["zones"].append(
            {
                "file": path.name,
                "body_bytes_consumed": pos,
                "virtual_bytes_required": virtual,
                "virtual_bytes_reserved": blocks[5],
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--map", default="mp_test")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    result = verify(args.game, args.package, args.map)
    text = json.dumps(result, indent=2)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    print(text)
