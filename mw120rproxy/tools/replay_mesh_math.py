"""Replay static-world tangent frame packing, shared by BSP and prop importers."""

import math


def quaternion(t, b, n):
    # Rotation columns are tangent, binormal, normal. Enforce a right-handed
    # orthonormal basis and carry mirrored UV handedness separately.
    dot = lambda a, b: sum(x * y for x, y in zip(a, b))
    cross = lambda a, b: [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
    norm = lambda a: [v / math.sqrt(dot(a, a)) for v in a]
    n = norm(n)
    t = norm([t[i] - dot(t, n) * n[i] for i in range(3)])
    cb = cross(n, t)
    sign = 1 if dot(cb, b) >= 0 else -1
    m = [[t[i], cb[i], n[i]] for i in range(3)]
    trace = sum(m[i][i] for i in range(3))
    if trace > 0:
        s = math.sqrt(trace + 1) * 2
        q = [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4]
    else:
        i = max(range(3), key=lambda i: m[i][i])
        j = (i + 1) % 3
        k = (i + 2) % 3
        s = math.sqrt(1 + m[i][i] - m[j][j] - m[k][k]) * 2
        q = [0.0] * 4
        q[i] = s / 4
        q[j] = (m[j][i] + m[i][j]) / s
        q[k] = (m[k][i] + m[i][k]) / s
        q[3] = (m[k][j] - m[j][k]) / s
    return q, sign


def pack(q, sign):
    # UnitQuatToQuatDec3n: largest component index in bits 30..31,
    # handedness bit 29, then 10/10/9 quantized remaining components.
    largest = max(range(4), key=lambda i: (abs(q[i]), i))
    scale = math.sqrt(2) * (1 if q[largest] >= 0 else -1)
    xyz = [q[i] * scale for i in range(4) if i != largest]
    ints = [
        max(0, min((1 << bits) - 1, int((v + 1) * ((1 << bits) - 1) / 2)))
        for v, bits in zip(xyz, (10, 10, 9))
    ]
    return ints[0] | ints[1] << 10 | ints[2] << 20 | (sign < 0) << 29 | largest << 30


def unpack_normal(packed):
    largest = packed >> 30
    values = [
        ((packed & 1023) / 1023 * 2 - 1) / math.sqrt(2),
        (((packed >> 10) & 1023) / 1023 * 2 - 1) / math.sqrt(2),
        (((packed >> 20) & 511) / 511 * 2 - 1) / math.sqrt(2),
    ]
    q = values[:]
    q.insert(largest, math.sqrt(max(0, 1 - sum(v * v for v in values))))
    x, y, z, w = q
    return [2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)]
