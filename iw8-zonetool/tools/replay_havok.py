"""Offline Replay Havok constructors and serialization. Never attach to a running game."""

import ctypes as C
import hashlib
import re
import struct

import pefile


class ReplayHavok:
    IMAGE_SHA256 = "68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0"
    SHAPE_LIST_TYPE = 0x4621020

    def __init__(self, executable):
        if hashlib.sha256(executable.read_bytes()).hexdigest() != self.IMAGE_SHA256:
            raise ValueError(
                "Collision baking requires Replay 1.20.4.7623265's original executable"
            )
        self.allocations = {}
        self.callbacks = []
        self.kernel = C.WinDLL("kernel32", use_last_error=True)
        self.crt = C.WinDLL("ucrtbase")
        pe = pefile.PE(str(executable), fast_load=True)
        allocate = self.kernel.VirtualAlloc
        allocate.restype = C.c_void_p
        allocate.argtypes = [C.c_void_p, C.c_size_t, C.c_ulong, C.c_ulong]
        self.base = allocate(
            pe.OPTIONAL_HEADER.ImageBase, pe.OPTIONAL_HEADER.SizeOfImage, 0x3000, 0x40
        )
        if self.base != pe.OPTIONAL_HEADER.ImageBase:
            raise RuntimeError(
                "Replay's preferred address is unavailable; use a fresh bake process"
            )
        for section in pe.sections:
            C.memmove(self.base + section.VirtualAddress, section.get_data(), section.SizeOfRawData)
        pe.parse_data_directories(directories=[1])
        for library in pe.DIRECTORY_ENTRY_IMPORT:
            if library.dll.lower() == b"kernel32.dll":
                for entry in library.imports:
                    address = C.cast(getattr(self.kernel, entry.name.decode()), C.c_void_p).value
                    self.put(entry.address, "Q", address)

        # Supply host allocation, TLS and CRT services without running game startup.
        heap, vtable = self.alloc(128), self.alloc(128)
        self.put(heap, "Q", vtable)
        alloc_block = self.callback(C.c_void_p, [C.c_void_p, C.c_int], lambda _, n: self.alloc(n))
        free_block = self.callback(None, [C.c_void_p, C.c_void_p, C.c_int], self.free)
        alloc_buffer = self.callback(
            C.c_void_p,
            [C.c_void_p, C.c_void_p],
            lambda _, n: self.alloc(C.c_int.from_address(n).value),
        )
        realloc_buffer = self.callback(
            C.c_void_p, [C.c_void_p, C.c_void_p, C.c_int, C.c_void_p], self.realloc
        )
        for offset, callback in (
            (8, alloc_block),
            (16, free_block),
            (24, alloc_buffer),
            (32, free_block),
            (40, realloc_buffer),
        ):
            self.put(vtable + offset, "Q", callback)
        self.put(self.base + 0x5A09F68, "Q", vtable)
        router = self.alloc(256)
        for offset in (0x50, 0x58, 0x60):
            self.put(router + offset, "Q", heap)
        stack_size = 16 * 1024 * 1024
        stack = self.alloc(stack_size)
        self.put(router + 0x10, "I", stack_size)
        self.put(router + 0x18, "3Q", stack, stack + stack_size, 0)
        self.put(self.base + 0x12D42A38, "Q", router)
        self.put(self.base + 0x12D42A40, "I", 1000)
        tls = self.callback(C.c_void_p, [C.c_ulong], lambda index: router if index == 1000 else 0)
        self.put(self.base + 0x23513B8, "Q", tls)
        printf = C.cast(self.crt.__getattr__("__stdio_common_vsnprintf_s"), C.c_void_p).value
        C.memmove(self.base + 0x220F0D4, b"\x48\xb8" + struct.pack("<Q", printf) + b"\xff\xe0", 12)

        self.function(0x1CCAC80, C.c_int, C.c_void_p)(self.base + 0x12D48EF8)
        code, start = pe.sections[0].get_data(), pe.sections[0].VirtualAddress
        registrations = 0
        for match in re.finditer(rb"\x48\x8d\x15....\x48\x8d\x0d....\xe9", code, re.S):
            offset = match.start()
            target = start + offset + 19 + struct.unpack_from("<i", code, offset + 15)[0]
            if target == 0x1C98120:
                self.function(start + offset, None)()
                registrations += 1
        if registrations != 2564:
            raise RuntimeError("Incomplete native reflection registration")
        self.function(0x1CE58F0, C.c_int, C.c_void_p)(self.base + 0x12D49C38)
        self.function(0x1C98160, C.c_int)()
        serializers = 0
        for match in re.finditer(rb"\x48\x8d\x0d....\xe8....", code, re.S):
            offset = match.start()
            obj = start + offset + 7 + struct.unpack_from("<i", code, offset + 3)[0]
            ctor = start + offset + 12 + struct.unpack_from("<i", code, offset + 8)[0]
            if ctor not in (0x1C96C40, 0x1C94350, 0x1C94310, 0x1C942D0) or obj < 0x29B1000:
                continue
            self.function(ctor, None, C.c_void_p, C.c_void_p)(self.base + obj, 0)
            if code[offset + 12 : offset + 15] == b"\x48\x8d\x05":
                vt = start + offset + 19 + struct.unpack_from("<i", code, offset + 15)[0]
                self.put(self.base + obj, "Q", self.base + vt)
            serializers += 1
        if serializers != 83:
            raise RuntimeError("Incomplete native serializer registration")
        self.function(0x365890, None)()  # Reflection copy dispatch table.
        pe.close()

    def alloc(self, size):
        if not 0 < size <= 256 * 1024 * 1024:
            raise ValueError(f"Invalid Havok allocation: {size}")
        storage = C.create_string_buffer(size + 16)
        address = (C.addressof(storage) + 15) & ~15
        self.allocations[address] = storage
        return address

    def free(self, _, address, size):
        self.allocations.pop(address, None)

    def realloc(self, _, old, old_size, size_pointer):
        size = C.c_int.from_address(size_pointer).value
        address = self.alloc(size)
        if old:
            C.memmove(address, old, min(size, old_size))
            self.allocations.pop(old, None)
        return address

    @staticmethod
    def put(address, fmt, *values):
        data = struct.pack("<" + fmt, *values)
        C.memmove(address, data, len(data))

    @staticmethod
    def pointer(address):
        return C.c_uint64.from_address(address).value

    def function(self, rva, result, *args):
        return C.WINFUNCTYPE(result, *args)(self.base + rva)

    def callback(self, result, args, function):
        callback = C.WINFUNCTYPE(result, *args)(function)
        self.callbacks.append(callback)
        return C.cast(callback, C.c_void_p).value

    def save(self, root, capacity):
        writer = self.alloc(32)
        self.function(0x1CB8B60, C.c_void_p, C.c_void_p, C.c_void_p)(writer, 0)
        output, config, buffer = self.alloc(capacity), self.alloc(32), self.alloc(64)
        self.put(config, "4Q", 0, output, capacity, 0)
        self.function(0x1C9A690, None, C.c_void_p)(buffer)
        self.function(0x1C9AA50, None, C.c_void_p, C.c_void_p)(buffer, config)
        self.function(0x1CB9B90, None, C.c_void_p, C.c_void_p)(writer, buffer)
        objects, indices, errors = [], {}, []
        next_id = 1

        def reserve(_):
            nonlocal next_id
            result = next_id
            next_id += 1
            return result

        def index(_, obj):
            address, typ = struct.unpack("<2Q", C.string_at(obj, 16))
            if not address:
                return 0
            key = address, typ
            if key not in indices:
                indices[key] = reserve(0)
                objects.append(key)
            return indices[key]

        def reference(_, obj):
            try:
                address, typ = struct.unpack("<2Q", C.string_at(obj, 16))
                target = self.pointer(address)
                if not target:
                    return 0
                # All non-null pointers in this graph are native hknp/hkReferencedObject shapes.
                typed = self.alloc(24)
                method = self.pointer(self.pointer(target)) - self.base
                self.function(method, C.c_void_p, C.c_void_p, C.c_void_p)(target, typed)
                result = index(0, typed)
                self.allocations.pop(typed)
                return result
            except Exception as error:
                errors.append(error)
                return 0

        provider, vtable = self.alloc(32), self.alloc(32)
        self.put(provider, "Q", vtable)
        self.put(
            vtable,
            "4Q",
            self.callback(C.c_int, [C.c_void_p, C.c_void_p], index),
            self.callback(C.c_int, [C.c_void_p, C.c_void_p], reference),
            self.callback(None, [C.c_void_p, C.c_int], lambda *_: None),
            self.callback(C.c_int, [C.c_void_p], reserve),
        )
        typed, status = self.alloc(24), self.alloc(16)
        self.put(typed, "3Q", root, self.base + self.SHAPE_LIST_TYPE, 0)
        index(0, typed)
        for address, typ in objects:
            self.put(typed, "3Q", address, typ, 0)
            self.function(0x1CBB310, C.c_void_p, *([C.c_void_p] * 4))(
                writer, status, typed, provider
            )
            if errors or C.c_int.from_address(status).value < 0:
                raise RuntimeError(f"Native collision serialization failed: {errors}")
        self.function(0x1CBA7A0, None, C.c_void_p)(writer)
        size = self.pointer(buffer) - output
        if not 0 < size <= capacity:
            raise RuntimeError("Native collision output exceeded the buffer")
        return C.string_at(output, size)

    def load(self, data):
        buffer, context, result = self.alloc(len(data)), self.alloc(32), self.alloc(24)
        C.memmove(buffer, data, len(data))
        self.function(0x1C9B480, C.c_void_p, C.c_void_p)(context)
        self.function(
            0x1C9CF00, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p
        )(context, result, buffer, len(data), self.base + self.SHAPE_LIST_TYPE)
        root = self.pointer(result)
        if not root:
            raise RuntimeError("Replay rejected the serialized collision")
        return root
