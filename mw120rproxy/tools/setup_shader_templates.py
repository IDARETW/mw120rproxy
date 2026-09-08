"""Read the required shaders from the user's exact Replay build; no process writes.

Start a stock Shipment Local Play match yourself before running this tool.
Generated game bytecode stays under ignored custom_map_sources, outside source control.
"""

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_MD5 = "1c238fe327f2ecc3b0db924c5b425439"


def validate_program(shader, program):
    expected = struct.unpack_from("<I", bytes.fromhex(shader["header"]), 32)[0]
    if len(program) != expected or hashlib.sha256(program).hexdigest() != shader["program_sha256"]:
        raise ValueError("Shader does not match the supported Replay build: " + shader["name"])
    if program and not program.startswith(b"DXBC"):
        raise ValueError("Expected DXBC shader")


def capture(pid, game):
    expected = (game / "game_dx12_ship_replay.exe").resolve(strict=True)
    if hashlib.md5(expected.read_bytes()).hexdigest() != EXPECTED_MD5:
        raise ValueError("Unsupported Replay executable")
    k = C.WinDLL("kernel32", use_last_error=True)
    ps = C.WinDLL("psapi", use_last_error=True)
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    k.ReadProcessMemory.argtypes = [
        W.HANDLE,
        C.c_void_p,
        C.c_void_p,
        C.c_size_t,
        C.POINTER(C.c_size_t),
    ]
    k.ReadProcessMemory.restype = W.BOOL
    k.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
    ps.EnumProcessModulesEx.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), W.DWORD]
    k.CloseHandle.argtypes = [W.HANDLE]
    handle = k.OpenProcess(0x410, False, pid)  # QUERY_INFORMATION | VM_READ only
    if not handle:
        raise C.WinError(C.get_last_error())
    try:
        name = C.create_unicode_buffer(32768)
        n = W.DWORD(len(name))
        if not k.QueryFullProcessImageNameW(handle, 0, name, C.byref(n)):
            raise C.WinError(C.get_last_error())
        if Path(name.value).resolve() != expected:
            raise ValueError("PID belongs to another executable")
        modules = (C.c_void_p * 1024)()
        needed = W.DWORD()
        if not ps.EnumProcessModulesEx(handle, modules, C.sizeof(modules), C.byref(needed), 3):
            raise C.WinError(C.get_last_error())
        base = modules[0]

        def read(ptr, size):
            if not ptr or not 0 < size <= 1024 * 1024:
                raise ValueError("Invalid bounded shader read")
            buf = C.create_string_buffer(size)
            got = C.c_size_t()
            if not k.ReadProcessMemory(handle, ptr, buf, size, C.byref(got)) or got.value != size:
                raise C.WinError(C.get_last_error())
            return buf.raw

        def q(buf, off=0):
            return struct.unpack_from("<Q", buf, off)[0]

        def u(buf, off=0):
            return struct.unpack_from("<I", buf, off)[0]

        def asset(kind, name):
            value = kind
            for byte in name.encode("ascii"):
                value = (value * 31 + byte) & 0xFFFFFFFF
            table = base + 0xC815C60
            index = u(read(table + 0x6E0F08 + (value % 0x57800) * 4, 4))
            visited = set()
            for _ in range(1024):
                if not index:
                    return 0
                if index >= 0x6E0F08 // 20 or index in visited:
                    raise ValueError("Invalid/cyclic asset chain")
                visited.add(index)
                entry = read(table + index * 20, 20)
                ptr = q(entry)
                if (
                    entry[17] == kind
                    and read(q(read(ptr, 8)), len(name) + 1) == name.encode() + b"\0"
                ):
                    return ptr
                index = u(entry, 8)
            raise ValueError("Asset lookup limit exceeded")

        graph = json.loads((ROOT / "mw120rproxy/data/static_world_recipe.json").read_text())
        for shader in graph["shaders"].values():
            size = u(bytes.fromhex(shader["header"]), 32)
            if size:
                ptr = asset(shader["type"], shader["name"])
                if not ptr:
                    raise ValueError(
                        "Shader is not resident; load stock Shipment first: " + shader["name"]
                    )
                header = read(ptr, 40)
                if u(header, 32) != size:
                    raise ValueError("Unexpected native shader size")
                program = read(q(header, 24), size)
            else:
                program = b""
            validate_program(shader, program)
            shader.pop("program_sha256")
            shader["program"] = program.hex()
        target = ROOT / "custom_map_sources/mp_test"
        shaderfile = target / "shaders/replay_static_world_techset.json"
        material = target / "dump/maps/mp/mp_test.d3dbsp.material.json"
        if shaderfile.exists() or material.exists():
            raise FileExistsError(
                "Templates already exist; preserve or move them before running setup again"
            )
        shaderfile.parent.mkdir(parents=True, exist_ok=True)
        material.parent.mkdir(parents=True, exist_ok=True)
        shaderfile.write_text(json.dumps(graph, indent=2) + "\n")
        material.write_bytes((ROOT / "mw120rproxy/data/material_recipe.json").read_bytes())
        print("Validated local shader templates written to " + str(target))
    finally:
        k.CloseHandle(handle)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pid", type=int, required=True)
    p.add_argument("--game", type=Path, required=True)
    a = p.parse_args()
    capture(a.pid, a.game)
