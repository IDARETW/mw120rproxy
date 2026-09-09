"""Build an atlas-aware prepass with the color pass's exact vertex and alpha coverage."""

import hashlib
import re
import struct


def _coverage_prefix(color_source, entry_point):
    signature = re.search(r"float4\s+main\s*\(\s*Input\s+input\s*\)\s*:\s*SV_TARGET0", color_source)
    cutoff = re.search(
        r"if\s*\(\s*kind\s*==\s*3\s*\)\s*clip\s*\(\s*texel\.a\s*-\s*\.5\s*\)\s*;", color_source
    )
    if not signature or not cutoff or cutoff.start() < signature.end():
        raise ValueError("Expected the generated atlas shader's explicit alpha cutoff")
    prefix = (
        color_source[: signature.start()]
        + entry_point
        + color_source[signature.end() : cutoff.end()]
    )
    return prefix


def prepass_source(color_source):
    """Keep the generated shader's sampling code, including its mip and cutoff rules."""
    return _coverage_prefix(color_source, "float main(Input input) : SV_TARGET1") + """
    float3 normal = normalize(input.normal);
    // Replay's prepass stores the geometric normal for screen-space resolves.
    // Tangent-space detail belongs only to the material lighting pass.
    uint3 packed = (uint3)(saturate(normal * .5 + .5) * 1023.0);
    return asfloat(packed.x | (packed.y << 10) | (packed.z << 20));
}
"""


def shadow_source(color_source):
    """Depth-only shadow coverage uses the identical atlas and vertex-alpha test."""
    prefix = _coverage_prefix(color_source, "void main(Input input)")
    # Opaque triangles need no atlas read in a depth-only pass. Metadata is
    # constant across each triangle, so this does not diverge within a quad.
    prefix, replacements = re.subn(
        r"(uint\s+kind\s*=\s*flags\s*%\s*4\s*;)",
        r"\1\n    if (kind != 3) return;",
        prefix,
        count=1,
    )
    if replacements != 1:
        raise ValueError("Expected the generated atlas material-kind field")
    return prefix + "\n}\n"


def _shader(code, label):
    digest = hashlib.sha256(code).digest()
    name = "mw120r_" + label + "_" + digest.hex()[:24]
    return name, {
        "type": 17,
        "name": name,
        "debugName": "map_" + label + "_coverage.hlsl",
        "header": (
            bytes(32) + struct.pack("<II", len(code), int.from_bytes(digest[:4], "little"))
        ).hex(),
        "program": code.hex(),
    }


def _state_identity(technique, code):
    header = bytes.fromhex(technique["header"])
    states = bytearray.fromhex(technique["states"])
    for offset in range(0, len(states), 16):
        states[offset : offset + 8] = hashlib.sha256(
            code + header + bytes([offset])
        ).digest()[:8]
    technique["states"] = states.hex()


def install(techset, source, compile_shader):
    code = compile_shader(prepass_source(source).encode())
    name, shader = _shader(code, "depth")
    lit = techset["techniques"][-1]
    depth = techset["techniques"][0]
    if struct.unpack_from("<I", bytes.fromhex(depth["header"]), 8)[0] != 0:
        raise ValueError("Template is missing its depth prepass")
    depth["shaders"] = [lit["shaders"][0], None, None, name]
    depth["name"] = "TECHNIQUE_DEPTH_PREPASS_" + name
    header = bytearray.fromhex(depth["header"])
    header[0x9C] = 35
    header[0x7A] = 4
    # The existing vertex constants and world buffer plus the color atlas.
    depth["args"] += struct.pack("<BBHH", 5, 16, 1, 0).hex()
    depth["header"] = header.hex()
    _state_identity(depth, code)
    techset["shaders"]["17:" + name] = shader
    shadow_code = compile_shader(shadow_source(source).encode())
    shadow_name, shader = _shader(shadow_code, "shadow")
    shadow_types = []
    for technique in techset["techniques"]:
        header = bytearray.fromhex(technique["header"])
        technique_type = struct.unpack_from("<I", header, 8)[0]
        if technique_type not in (27, 28):
            continue
        # Layout 35 binds both world position t14 and auxiliary t15 buffers in
        # Replay's R_DrawBspSurf; projection uses the stock shadow CB2 matrix.
        # Keep the native depth bias, depth write, and zero color-target count.
        header[0x9C] = 35
        header[0x7A] = 4
        technique["header"] = header.hex()
        technique["args"] += struct.pack("<BBHH", 5, 16, 1, 0).hex()
        technique["shaders"] = [lit["shaders"][0], None, None, shadow_name]
        technique["name"] = "TECHNIQUE_SHADOW_" + str(technique_type) + "_" + shadow_name
        _state_identity(technique, shadow_code)
        shadow_types.append(technique_type)
    if sorted(shadow_types) != [27, 28]:
        raise ValueError("Template is missing its front/back shadow techniques")
    techset["shaders"]["17:" + shadow_name] = shader
    th = bytearray.fromhex(techset["header"])
    # Material_GetPrepassType: bit 0x1000 substitutes the shared opaque prepass,
    # which cannot evaluate this atlas. Retain this material's own technique.
    flags = struct.unpack_from("<I", th, 8)[0] & ~0x1000
    struct.pack_into("<I", th, 8, flags)
    techset["header"] = th.hex()
    techset["coveragePrepass"] = True
    techset["coverageShadows"] = True
