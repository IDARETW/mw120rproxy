#include "replay_havok.h"

#include "../../common/fs_util.h"
#include "../../common/log.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace iw8::havok
{
namespace
{

constexpr char kReplaySha256[] = "68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0";
constexpr std::uintptr_t kShapeListType = 0x4621020;
constexpr std::size_t kMaximumAllocation = 256u * 1024u * 1024u;

template <typename T> T Read(const void *address)
{
    T value{};
    std::memcpy(&value, address, sizeof(value));
    return value;
}

template <typename T> void Write(void *address, const T &value)
{
    std::memcpy(address, &value, sizeof(value));
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path &path)
{
    std::vector<std::uint8_t> bytes;
    if (!zt::read_file(path.string(), bytes))
        throw std::runtime_error("Cannot read " + path.string());
    return bytes;
}

std::string Sha256(const std::vector<std::uint8_t> &bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    DWORD digestBytes = 0;
    std::vector<std::uint8_t> object;
    std::vector<std::uint8_t> digest;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
                          sizeof(objectBytes), &resultBytes, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digestBytes),
                          sizeof(digestBytes), &resultBytes, 0) < 0)
    {
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Cannot initialize SHA-256");
    }

    object.resize(objectBytes);
    digest.resize(digestBytes);
    const auto cleanup = [&] {
        if (hash)
            BCryptDestroyHash(hash);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
                       0) < 0 ||
        BCryptFinishHash(hash, digest.data(), digestBytes, 0) < 0)
    {
        cleanup();
        throw std::runtime_error("Cannot calculate SHA-256");
    }
    cleanup();

    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const auto byte : digest)
    {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

struct Hull
{
    std::vector<std::array<float, 3>> points;
    std::uint32_t contents = 1;
};

std::vector<Hull> ReadCollision(const std::filesystem::path &path)
{
    const auto bytes = ReadFile(path);
    if (bytes.size() < 12 || (std::memcmp(bytes.data(), "MWCOLL02", 8) != 0 &&
                              std::memcmp(bytes.data(), "MWCOLL03", 8) != 0))
        throw std::runtime_error("Collision input must use MWCOLL02 or MWCOLL03");

    const bool tagged = std::memcmp(bytes.data(), "MWCOLL03", 8) == 0;
    const auto count = Read<std::uint32_t>(bytes.data() + 8);
    if (count == 0 || count > 32768)
        throw std::runtime_error("Collision hull count is invalid");

    std::size_t cursor = 12;
    std::vector<Hull> hulls;
    hulls.reserve(count);
    constexpr std::uint32_t supportedContents = 0x33681;
    for (std::uint32_t hullIndex = 0; hullIndex < count; ++hullIndex)
    {
        if (cursor + (tagged ? 8u : 4u) > bytes.size())
            throw std::runtime_error("Collision input is truncated");
        Hull hull;
        const auto vertexCount = Read<std::uint32_t>(bytes.data() + cursor);
        cursor += 4;
        if (tagged)
        {
            hull.contents = Read<std::uint32_t>(bytes.data() + cursor);
            cursor += 4;
        }
        if (vertexCount < 4 || vertexCount > 252 || hull.contents == 0 ||
            (hull.contents & ~supportedContents) != 0 ||
            cursor + std::size_t(vertexCount) * 12 > bytes.size())
            throw std::runtime_error("Collision hull data is invalid");
        hull.points.resize(vertexCount);
        std::memcpy(hull.points.data(), bytes.data() + cursor, std::size_t(vertexCount) * 12);
        cursor += std::size_t(vertexCount) * 12;
        for (const auto &point : hull.points)
            for (const auto value : point)
                if (!std::isfinite(value))
                    throw std::runtime_error("Collision contains a non-finite vertex");
        hulls.push_back(std::move(hull));
    }
    if (cursor != bytes.size())
        throw std::runtime_error("Collision input has trailing data");
    return hulls;
}

struct FloorTriangle
{
    std::array<float, 3> origin{};
    std::array<float, 3> u{};
    std::array<float, 3> v{};
    float determinant = 0;
    std::uint32_t material = 5;
};

class FloorMaterials
{
  public:
    explicit FloorMaterials(const std::filesystem::path &path)
    {
        if (path.empty())
            return;
        const auto bytes = ReadFile(path);
        if (bytes.size() < 12 || std::memcmp(bytes.data(), "MWRSTEP1", 8) != 0)
            throw std::runtime_error("Footstep input must use MWRSTEP1");
        const auto count = Read<std::uint32_t>(bytes.data() + 8);
        if (bytes.size() != 12 + std::size_t(count) * 40)
            throw std::runtime_error("Footstep input length is invalid");

        const auto *cursor = bytes.data() + 12;
        for (std::uint32_t i = 0; i < count; ++i, cursor += 40)
        {
            FloorTriangle triangle;
            std::array<float, 9> values{};
            std::memcpy(values.data(), cursor, sizeof(values));
            triangle.material = Read<std::uint32_t>(cursor + 36);
            if (triangle.material < 1 || triangle.material > 28 ||
                std::any_of(values.begin(), values.end(),
                            [](float value) { return !std::isfinite(value); }))
                throw std::runtime_error("Footstep triangle is invalid");
            std::copy_n(values.begin(), 3, triangle.origin.begin());
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                triangle.u[axis] = values[3 + axis] - values[axis];
                triangle.v[axis] = values[6 + axis] - values[axis];
            }
            triangle.determinant = triangle.u[0] * triangle.v[1] - triangle.u[1] * triangle.v[0];
            if (std::abs(triangle.determinant) < 0.001f)
                continue;

            const auto minimumX = (std::min)({values[0], values[3], values[6]});
            const auto maximumX = (std::max)({values[0], values[3], values[6]});
            const auto minimumY = (std::min)({values[1], values[4], values[7]});
            const auto maximumY = (std::max)({values[1], values[4], values[7]});
            const auto x0 = static_cast<int>(std::floor(minimumX / 128.0f));
            const auto x1 = static_cast<int>(std::floor(maximumX / 128.0f));
            const auto y0 = static_cast<int>(std::floor(minimumY / 128.0f));
            const auto y1 = static_cast<int>(std::floor(maximumY / 128.0f));
            if (std::int64_t(x1 - x0 + 1) * std::int64_t(y1 - y0 + 1) > 100000)
                throw std::runtime_error("Footstep triangle exceeds map limits");
            const auto triangleIndex = triangles_.size();
            triangles_.push_back(triangle);
            for (int x = x0; x <= x1; ++x)
                for (int y = y0; y <= y1; ++y)
                    cells_[Key(x, y)].push_back(triangleIndex);
        }
    }

    std::uint32_t At(float x, float y, float z) const
    {
        const auto cell = cells_.find(Key(static_cast<int>(std::floor(x / 128.0f)),
                                          static_cast<int>(std::floor(y / 128.0f))));
        if (cell == cells_.end())
            return 5;
        float best = 12.01f;
        std::uint32_t material = 5;
        for (const auto index : cell->second)
        {
            const auto &triangle = triangles_[index];
            const float dx = x - triangle.origin[0];
            const float dy = y - triangle.origin[1];
            const float s = (dx * triangle.v[1] - dy * triangle.v[0]) / triangle.determinant;
            const float t = (triangle.u[0] * dy - triangle.u[1] * dx) / triangle.determinant;
            if (s < -0.002f || t < -0.002f || s + t > 1.002f)
                continue;
            const float distance =
                std::abs(z - triangle.origin[2] - s * triangle.u[2] - t * triangle.v[2]);
            if (distance < best)
            {
                best = distance;
                material = triangle.material;
            }
        }
        return material;
    }

  private:
    static std::uint64_t Key(int x, int y)
    {
        return (std::uint64_t(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
    }

    std::vector<FloorTriangle> triangles_;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells_;
};

class ReplayHavok;
thread_local ReplayHavok *g_activeHavok = nullptr;

struct SaveContext
{
    ReplayHavok *havok = nullptr;
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> objects;
    std::map<std::pair<std::uintptr_t, std::uintptr_t>, int> indices;
    int nextId = 1;
    bool failed = false;
};
thread_local SaveContext *g_saveContext = nullptr;

class ReplayHavok
{
  public:
    explicit ReplayHavok(const std::filesystem::path &executable)
    {
        const auto file = ReadFile(executable);
        if (Sha256(file) != kReplaySha256)
            throw std::runtime_error(
                "Collision baking requires the original Replay 1.20.4.7623265 executable");
        if (file.size() < sizeof(IMAGE_DOS_HEADER))
            throw std::runtime_error("Replay executable is truncated");

        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(file.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            std::size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > file.size())
            throw std::runtime_error("Replay executable has an invalid PE header");
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(file.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            throw std::runtime_error("Replay executable is not PE32+");

        base_ = static_cast<std::uint8_t *>(VirtualAlloc(
            reinterpret_cast<void *>(nt->OptionalHeader.ImageBase), nt->OptionalHeader.SizeOfImage,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (reinterpret_cast<std::uintptr_t>(base_) != nt->OptionalHeader.ImageBase)
            throw std::runtime_error(
                "Replay's preferred address is unavailable; use a fresh conversion process");
        imageSize_ = nt->OptionalHeader.SizeOfImage;
        std::memcpy(base_, file.data(),
                    (std::min<std::size_t>)(nt->OptionalHeader.SizeOfHeaders, file.size()));

        const auto *sections = IMAGE_FIRST_SECTION(nt);
        for (std::uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto &section = sections[i];
            if (std::size_t(section.PointerToRawData) + section.SizeOfRawData > file.size() ||
                std::size_t(section.VirtualAddress) + section.SizeOfRawData > imageSize_)
                throw std::runtime_error("Replay section lies outside the image");
            std::memcpy(base_ + section.VirtualAddress, file.data() + section.PointerToRawData,
                        section.SizeOfRawData);
        }
        PatchKernelImports(nt);

        g_activeHavok = this;
        auto *heap = Allocate(128);
        auto *vtable = Allocate(128);
        Write(heap, reinterpret_cast<std::uintptr_t>(vtable));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 8,
              reinterpret_cast<std::uintptr_t>(&AllocateBlock));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 16,
              reinterpret_cast<std::uintptr_t>(&FreeBlock));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 24,
              reinterpret_cast<std::uintptr_t>(&AllocateBuffer));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 32,
              reinterpret_cast<std::uintptr_t>(&FreeBlock));
        Write(reinterpret_cast<std::uint8_t *>(vtable) + 40,
              reinterpret_cast<std::uintptr_t>(&ReallocateBuffer));
        Write(Address(0x5A09F68), reinterpret_cast<std::uintptr_t>(vtable));

        auto *router = Allocate(256);
        for (const auto offset : {0x50, 0x58, 0x60})
            Write(reinterpret_cast<std::uint8_t *>(router) + offset,
                  reinterpret_cast<std::uintptr_t>(heap));
        constexpr std::uint32_t stackSize = 16u * 1024u * 1024u;
        auto *stack = Allocate(stackSize);
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x10, stackSize);
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x18,
              reinterpret_cast<std::uintptr_t>(stack));
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x20,
              reinterpret_cast<std::uintptr_t>(stack) + stackSize);
        Write(reinterpret_cast<std::uint8_t *>(router) + 0x28, std::uintptr_t{});
        router_ = router;
        Write(Address(0x12D42A38), reinterpret_cast<std::uintptr_t>(router));
        Write(Address(0x12D42A40), std::uint32_t{1000});
        Write(Address(0x23513B8), reinterpret_cast<std::uintptr_t>(&TlsValue));

        auto *crt = LoadLibraryW(L"ucrtbase.dll");
        const auto formatter =
            reinterpret_cast<std::uintptr_t>(GetProcAddress(crt, "__stdio_common_vsnprintf_s"));
        if (!formatter)
            throw std::runtime_error("ucrtbase formatter is unavailable");
        std::array<std::uint8_t, 12> thunk{0x48, 0xB8};
        std::memcpy(thunk.data() + 2, &formatter, sizeof(formatter));
        thunk[10] = 0xFF;
        thunk[11] = 0xE0;
        std::memcpy(Address(0x220F0D4), thunk.data(), thunk.size());

        Function<int(void *)>(0x1CCAC80)(Address(0x12D48EF8));
        const auto &codeSection = sections[0];
        const auto *code = file.data() + codeSection.PointerToRawData;
        const auto codeSize = std::size_t(codeSection.SizeOfRawData);
        const auto codeRva = std::uintptr_t(codeSection.VirtualAddress);

        std::size_t registrations = 0;
        for (std::size_t offset = 0; offset + 19 <= codeSize; ++offset)
        {
            if (code[offset] != 0x48 || code[offset + 1] != 0x8D || code[offset + 2] != 0x15 ||
                code[offset + 7] != 0x48 || code[offset + 8] != 0x8D || code[offset + 9] != 0x0D ||
                code[offset + 14] != 0xE9)
                continue;
            const auto target = codeRva + offset + 19 + Read<std::int32_t>(code + offset + 15);
            if (target == 0x1C98120)
            {
                Function<void()>(codeRva + offset)();
                ++registrations;
            }
        }
        if (registrations != 2564)
            throw std::runtime_error("Incomplete native reflection registration");
        Function<int(void *)>(0x1CE58F0)(Address(0x12D49C38));
        Function<int()>(0x1C98160)();

        std::size_t serializers = 0;
        for (std::size_t offset = 0; offset + 19 <= codeSize; ++offset)
        {
            if (code[offset] != 0x48 || code[offset + 1] != 0x8D || code[offset + 2] != 0x0D ||
                code[offset + 7] != 0xE8)
                continue;
            const auto object = codeRva + offset + 7 + Read<std::int32_t>(code + offset + 3);
            const auto constructor = codeRva + offset + 12 + Read<std::int32_t>(code + offset + 8);
            if ((constructor != 0x1C96C40 && constructor != 0x1C94350 && constructor != 0x1C94310 &&
                 constructor != 0x1C942D0) ||
                object < 0x29B1000)
                continue;
            Function<void(void *, void *)>(constructor)(Address(object), nullptr);
            if (code[offset + 12] == 0x48 && code[offset + 13] == 0x8D && code[offset + 14] == 0x05)
            {
                const auto vtableRva =
                    codeRva + offset + 19 + Read<std::int32_t>(code + offset + 15);
                Write(Address(object), reinterpret_cast<std::uintptr_t>(Address(vtableRva)));
            }
            ++serializers;
        }
        if (serializers != 83)
            throw std::runtime_error("Incomplete native serializer registration");
        Function<void()>(0x365890)();
    }

    ~ReplayHavok()
    {
        if (g_activeHavok == this)
            g_activeHavok = nullptr;
        for (const auto &[address, unused] : allocations_)
        {
            (void)unused;
            _aligned_free(address);
        }
        if (base_)
            VirtualFree(base_, 0, MEM_RELEASE);
    }

    ReplayHavok(const ReplayHavok &) = delete;
    ReplayHavok &operator=(const ReplayHavok &) = delete;

    void *Allocate(std::size_t bytes)
    {
        if (bytes == 0 || bytes > kMaximumAllocation)
            throw std::runtime_error("Havok allocation size is invalid");
        void *address = _aligned_malloc(bytes, 16);
        if (!address)
            throw std::bad_alloc();
        std::memset(address, 0, bytes);
        allocations_.emplace(address, bytes);
        return address;
    }

    void Release(void *address)
    {
        const auto allocation = allocations_.find(address);
        if (allocation == allocations_.end())
            return;
        _aligned_free(address);
        allocations_.erase(allocation);
    }

    template <typename Signature> auto Function(std::uintptr_t rva) const
    {
        return reinterpret_cast<Signature *>(base_ + rva);
    }

    void *Address(std::uintptr_t rva) const
    {
        return base_ + rva;
    }

    std::uintptr_t Base() const
    {
        return reinterpret_cast<std::uintptr_t>(base_);
    }

    std::vector<std::uint8_t> Save(void *root, std::size_t capacity)
    {
        auto *writer = Allocate(32);
        Function<void *(void *, void *)>(0x1CB8B60)(writer, nullptr);
        auto *output = static_cast<std::uint8_t *>(Allocate(capacity));
        auto *config = static_cast<std::uint8_t *>(Allocate(32));
        auto *buffer = Allocate(64);
        Write(config + 8, reinterpret_cast<std::uintptr_t>(output));
        Write(config + 16, static_cast<std::uint64_t>(capacity));
        Function<void(void *)>(0x1C9A690)(buffer);
        Function<void(void *, void *)>(0x1C9AA50)(buffer, config);
        Function<void(void *, void *)>(0x1CB9B90)(writer, buffer);

        SaveContext context;
        context.havok = this;
        g_saveContext = &context;
        auto *provider = static_cast<std::uint8_t *>(Allocate(32));
        auto *vtable = static_cast<std::uint8_t *>(Allocate(32));
        Write(provider, reinterpret_cast<std::uintptr_t>(vtable));
        Write(vtable, reinterpret_cast<std::uintptr_t>(&ProviderIndex));
        Write(vtable + 8, reinterpret_cast<std::uintptr_t>(&ProviderReference));
        Write(vtable + 16, reinterpret_cast<std::uintptr_t>(&ProviderWritten));
        Write(vtable + 24, reinterpret_cast<std::uintptr_t>(&ProviderReserve));
        auto *typed = static_cast<std::uint8_t *>(Allocate(24));
        auto *status = static_cast<std::uint8_t *>(Allocate(16));
        Write(typed, reinterpret_cast<std::uintptr_t>(root));
        Write(typed + 8, Base() + kShapeListType);
        ProviderIndex(nullptr, typed);

        for (std::size_t i = 0; i < context.objects.size(); ++i)
        {
            Write(typed, context.objects[i].first);
            Write(typed + 8, context.objects[i].second);
            Write(typed + 16, std::uintptr_t{});
            Function<void *(void *, void *, void *, void *)>(0x1CBB310)(writer, status, typed,
                                                                        provider);
            if (context.failed || Read<int>(status) < 0)
            {
                g_saveContext = nullptr;
                throw std::runtime_error("Native collision serialization failed");
            }
        }
        Function<void(void *)>(0x1CBA7A0)(writer);
        g_saveContext = nullptr;

        const auto end = Read<std::uintptr_t>(buffer);
        const auto begin = reinterpret_cast<std::uintptr_t>(output);
        if (end <= begin || end - begin > capacity)
            throw std::runtime_error("Native collision output exceeded its buffer");
        return {output, output + (end - begin)};
    }

    void *Load(const std::vector<std::uint8_t> &bytes)
    {
        auto *buffer = Allocate(bytes.size());
        std::memcpy(buffer, bytes.data(), bytes.size());
        auto *context = Allocate(32);
        auto *result = Allocate(24);
        Function<void *(void *)>(0x1C9B480)(context);
        Function<void *(void *, void *, void *, std::size_t, void *)>(0x1C9CF00)(
            context, result, buffer, bytes.size(), Address(kShapeListType));
        auto *root = reinterpret_cast<void *>(Read<std::uintptr_t>(result));
        if (!root)
            throw std::runtime_error("Replay rejected the serialized collision");
        return root;
    }

  private:
    static void *AllocateBlock(void *, int bytes)
    {
        try
        {
            return g_activeHavok->Allocate(bytes);
        }
        catch (...)
        {
            return nullptr;
        }
    }

    static void FreeBlock(void *, void *address, int)
    {
        if (g_activeHavok)
            g_activeHavok->Release(address);
    }

    static void *AllocateBuffer(void *, void *sizeAddress)
    {
        try
        {
            return g_activeHavok->Allocate(Read<int>(sizeAddress));
        }
        catch (...)
        {
            return nullptr;
        }
    }

    static void *ReallocateBuffer(void *, void *oldAddress, int oldSize, void *sizeAddress)
    {
        try
        {
            const auto bytes = Read<int>(sizeAddress);
            auto *replacement = g_activeHavok->Allocate(bytes);
            if (oldAddress)
            {
                std::memcpy(replacement, oldAddress, (std::min)(bytes, oldSize));
                g_activeHavok->Release(oldAddress);
            }
            return replacement;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    static void *WINAPI TlsValue(DWORD index)
    {
        return g_activeHavok && index == 1000 ? g_activeHavok->router_ : nullptr;
    }

    static int ProviderReserve(void *)
    {
        return g_saveContext ? g_saveContext->nextId++ : 0;
    }

    static int ProviderIndex(void *, void *object)
    {
        if (!g_saveContext)
            return 0;
        const auto address = Read<std::uintptr_t>(object);
        const auto type = Read<std::uintptr_t>(static_cast<std::uint8_t *>(object) + 8);
        if (!address)
            return 0;
        const auto key = std::make_pair(address, type);
        if (const auto found = g_saveContext->indices.find(key);
            found != g_saveContext->indices.end())
            return found->second;
        const auto id = ProviderReserve(nullptr);
        g_saveContext->indices.emplace(key, id);
        g_saveContext->objects.push_back(key);
        return id;
    }

    static int ProviderReference(void *, void *object)
    {
        if (!g_saveContext)
            return 0;
        try
        {
            const auto address = Read<std::uintptr_t>(object);
            const auto target =
                address ? Read<std::uintptr_t>(reinterpret_cast<void *>(address)) : 0;
            if (!target)
                return 0;
            auto *typed = static_cast<std::uint8_t *>(g_saveContext->havok->Allocate(24));
            const auto vtable = Read<std::uintptr_t>(reinterpret_cast<void *>(target));
            const auto method = Read<std::uintptr_t>(reinterpret_cast<void *>(vtable));
            g_saveContext->havok->Function<void *(void *, void *)>(
                method - g_saveContext->havok->Base())(reinterpret_cast<void *>(target), typed);
            const auto result = ProviderIndex(nullptr, typed);
            g_saveContext->havok->Release(typed);
            return result;
        }
        catch (...)
        {
            g_saveContext->failed = true;
            return 0;
        }
    }

    static void ProviderWritten(void *, int) {}

    void PatchKernelImports(const IMAGE_NT_HEADERS64 *nt)
    {
        const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress || !directory.Size)
            throw std::runtime_error("Replay executable has no imports");
        auto *descriptor =
            reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base_ + directory.VirtualAddress);
        for (; descriptor->Name; ++descriptor)
        {
            const auto *library = reinterpret_cast<const char *>(base_ + descriptor->Name);
            if (_stricmp(library, "kernel32.dll") != 0)
                continue;
            auto *names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(
                base_ + (descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk
                                                        : descriptor->FirstThunk));
            auto *addresses =
                reinterpret_cast<IMAGE_THUNK_DATA64 *>(base_ + descriptor->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++addresses)
            {
                FARPROC function = nullptr;
                if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
                {
                    function = GetProcAddress(
                        GetModuleHandleW(L"kernel32.dll"),
                        reinterpret_cast<const char *>(IMAGE_ORDINAL64(names->u1.Ordinal)));
                }
                else
                {
                    const auto *import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(
                        base_ + names->u1.AddressOfData);
                    function = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), import->Name);
                }
                if (!function)
                    throw std::runtime_error("Replay kernel32 import is unavailable");
                addresses->u1.Function = reinterpret_cast<ULONGLONG>(function);
            }
        }
    }

    std::uint8_t *base_ = nullptr;
    std::size_t imageSize_ = 0;
    void *router_ = nullptr;
    std::unordered_map<void *, std::size_t> allocations_;
};

template <typename T>
void WriteArray(ReplayHavok &havok, std::uint8_t *root, std::size_t offset,
                const std::vector<T> &values)
{
    auto *memory = static_cast<std::uint8_t *>(havok.Allocate(sizeof(T) * values.size()));
    if (!values.empty())
        std::memcpy(memory, values.data(), sizeof(T) * values.size());
    Write(root + offset, reinterpret_cast<std::uintptr_t>(memory));
    Write(root + offset + 8, static_cast<std::uint32_t>(values.size()));
    Write(root + offset + 12, static_cast<std::uint32_t>(values.size()) | 0x80000000u);
}

#pragma pack(push, 1)
struct ShapeTag
{
    std::uint32_t contents;
    std::uint32_t hash;
    std::uint16_t collisionFilterInfo;
    std::uint8_t padding[6]{};
    std::uint64_t userData;
};
#pragma pack(pop)
static_assert(sizeof(ShapeTag) == 24);

} // namespace

std::vector<std::uint8_t> BakeCollision(const BakeInput &input)
{
    const auto hulls = ReadCollision(input.collision);
    const FloorMaterials floors(input.footsteps);
    ReplayHavok havok(input.replayExecutable);

    auto *config = static_cast<std::uint8_t *>(havok.Allocate(96));
    havok.Function<void *(void *)>(0x1E81300)(config);
    Write(config, 0.0f);
    config[4] = 0;
    config[16] = 1;
    config[17] = 0;

    auto *instances = static_cast<std::uint8_t *>(havok.Allocate(hulls.size() * 112));
    auto *vertices = static_cast<std::uint8_t *>(havok.Allocate(252 * 16));
    auto *vertexArray = static_cast<std::uint8_t *>(havok.Allocate(16));
    std::vector<void *> nativeHulls;
    std::vector<ShapeTag> tags;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint16_t> tagIndices;
    std::array<float, 3> minimum{std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::infinity()};
    std::array<float, 3> maximum{-std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity()};
    std::uint32_t allContents = 0;
    std::uint32_t totalVertices = 0;
    std::uint32_t totalTriangles = 0;

    for (std::size_t index = 0; index < hulls.size(); ++index)
    {
        const auto &hull = hulls[index];
        std::array<float, 3> center{};
        for (const auto &point : hull.points)
            for (std::size_t axis = 0; axis < 3; ++axis)
                center[axis] += point[axis];
        for (auto &value : center)
            value /= static_cast<float>(hull.points.size());
        const auto highest = std::max_element(
            hull.points.begin(), hull.points.end(),
            [](const auto &left, const auto &right) { return left[2] < right[2]; });
        const auto material = floors.At(center[0], center[1], (*highest)[2]);
        const auto key = std::make_pair(hull.contents, material);
        auto tag = tagIndices.find(key);
        if (tag == tagIndices.end())
        {
            const auto newIndex = static_cast<std::uint16_t>(tags.size());
            const std::uint64_t userData = (std::uint64_t((hull.contents & 1) ? 1 : 3) << 48) |
                                           (std::uint64_t(material) << 19);
            tags.push_back({hull.contents, 0x1AB7BC33u, 0xFFFFu, {}, userData});
            tag = tagIndices.emplace(key, newIndex).first;
        }
        allContents |= hull.contents;

        for (std::size_t vertex = 0; vertex < hull.points.size(); ++vertex)
        {
            std::array<float, 4> converted{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                converted[axis] = (hull.points[vertex][axis] - center[axis]) / 32.0f;
                minimum[axis] = (std::min)(minimum[axis], hull.points[vertex][axis] / 32.0f);
                maximum[axis] = (std::max)(maximum[axis], hull.points[vertex][axis] / 32.0f);
            }
            std::memcpy(vertices + vertex * 16, converted.data(), 16);
        }
        Write(vertexArray, reinterpret_cast<std::uintptr_t>(vertices));
        Write(vertexArray + 8, static_cast<std::uint32_t>(hull.points.size()));
        Write(vertexArray + 12, std::uint32_t{16});
        auto *shape =
            havok.Function<void *(void *, float, void *)>(0x1E82060)(vertexArray, 0.0f, config);
        if (!shape)
            throw std::runtime_error("Native convex construction failed at hull " +
                                     std::to_string(index));
        nativeHulls.push_back(shape);
        totalVertices += static_cast<std::uint32_t>(hull.points.size());
        totalTriangles += static_cast<std::uint32_t>(hull.points.size() * 2 - 4);

        auto *instance = instances + index * 112;
        const std::array<float, 16> transform{1,
                                              0,
                                              0,
                                              0,
                                              0,
                                              1,
                                              0,
                                              0,
                                              0,
                                              0,
                                              1,
                                              0,
                                              center[0] / 32.0f,
                                              center[1] / 32.0f,
                                              center[2] / 32.0f,
                                              1};
        std::memcpy(instance, transform.data(), sizeof(transform));
        Write(instance + 12, std::uint32_t{0x3F000040});
        const std::array<float, 4> scale{1, 1, 1, 1};
        std::memcpy(instance + 64, scale.data(), sizeof(scale));
        Write(instance + 80, reinterpret_cast<std::uintptr_t>(shape));
        Write(instance + 88, tag->second);
        Write(instance + 90, std::uint16_t{0xFFFF});
        Write(instance + 100, std::uint16_t{0xFFFF});
        if ((index + 1) % 1000 == 0)
            zt::info("collision: built %zu/%zu hulls", index + 1, hulls.size());
    }

    auto *array = static_cast<std::uint8_t *>(havok.Allocate(16));
    Write(array, reinterpret_cast<std::uintptr_t>(instances));
    Write(array + 8, static_cast<std::uint32_t>(hulls.size()));
    Write(array + 12, static_cast<std::uint32_t>(hulls.size()) | 0x80000000u);
    auto *world = havok.Function<void *(void *)>(0x161C770)(array);
    if (!world)
        throw std::runtime_error("Native compound construction failed");

    auto *root = static_cast<std::uint8_t *>(havok.Allocate(152));
    auto *name = static_cast<char *>(havok.Allocate(32));
    std::memcpy(name, "World Entity Main_Full", 23);
    WriteArray(havok, root, 0,
               std::vector<std::uintptr_t>{reinterpret_cast<std::uintptr_t>(world)});
    WriteArray(havok, root, 16, std::vector<std::int32_t>{-1});
    WriteArray(havok, root, 32,
               std::vector<std::uintptr_t>{reinterpret_cast<std::uintptr_t>(name)});
    WriteArray(havok, root, 48, std::vector<std::uint32_t>{totalVertices});
    WriteArray(havok, root, 64, std::vector<std::uint32_t>{totalTriangles});
    WriteArray(havok, root, 80,
               std::vector<std::array<float, 4>>{{minimum[0], minimum[1], minimum[2], 0},
                                                 {maximum[0], maximum[1], maximum[2], 0}});
    WriteArray(havok, root, 104, tags);
    WriteArray(havok, root, 120, std::vector<std::uint32_t>{allContents});
    WriteArray(havok, root, 136,
               std::vector<std::uint32_t>{static_cast<std::uint32_t>(hulls.size())});

    const auto capacity = (std::min)(kMaximumAllocation, 1024u * 1024u + hulls.size() * 8192u);
    auto output = havok.Save(root, capacity);
    auto *loaded = static_cast<std::uint8_t *>(havok.Load(output));
    const auto restored =
        Read<std::uintptr_t>(reinterpret_cast<void *>(Read<std::uintptr_t>(loaded)));
    if (Read<std::uint32_t>(loaded + 8) != 1 ||
        Read<std::uint32_t>(reinterpret_cast<void *>(restored + 80)) != hulls.size() ||
        Read<std::uint32_t>(loaded + 112) != tags.size())
        throw std::runtime_error("Native collision round trip lost shapes or tags");
    if (std::memcmp(reinterpret_cast<void *>(Read<std::uintptr_t>(loaded + 104)),
                    reinterpret_cast<void *>(Read<std::uintptr_t>(root + 104)),
                    tags.size() * sizeof(ShapeTag)) != 0)
        throw std::runtime_error("Native collision round trip changed shape tags");

    const auto restoredChildren = Read<std::uintptr_t>(reinterpret_cast<void *>(restored + 72));
    const auto originalChildren = Read<std::uintptr_t>(static_cast<std::uint8_t *>(world) + 72);
    auto *queryBounds = havok.Allocate(32);
    for (std::size_t i = 0; i < nativeHulls.size(); ++i)
    {
        const auto child =
            Read<std::uintptr_t>(reinterpret_cast<void *>(restoredChildren + i * 112 + 80));
        if (!child ||
            std::memcmp(reinterpret_cast<void *>(child + 24),
                        static_cast<std::uint8_t *>(nativeHulls[i]) + 24, 24) != 0 ||
            std::memcmp(reinterpret_cast<void *>(restoredChildren + i * 112),
                        reinterpret_cast<void *>(originalChildren + i * 112), 80) != 0)
            throw std::runtime_error("Native collision round trip changed hull " +
                                     std::to_string(i));
        const auto vtable = Read<std::uintptr_t>(reinterpret_cast<void *>(child));
        const auto method = Read<std::uintptr_t>(reinterpret_cast<void *>(vtable + 32));
        havok.Function<void(void *, void *, void *)>(method - havok.Base())(
            reinterpret_cast<void *>(child), reinterpret_cast<void *>(restoredChildren + i * 112),
            queryBounds);
        const auto *actual = static_cast<const float *>(queryBounds);
        std::array<float, 6> expected{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            expected[axis] = hulls[i].points.front()[axis] / 32.0f;
            expected[axis + 3] = expected[axis];
            for (const auto &point : hulls[i].points)
            {
                expected[axis] = (std::min)(expected[axis], point[axis] / 32.0f);
                expected[axis + 3] = (std::max)(expected[axis + 3], point[axis] / 32.0f);
            }
        }
        const std::array<float, 6> measured{actual[0], actual[1], actual[2],
                                            actual[4], actual[5], actual[6]};
        for (std::size_t axis = 0; axis < measured.size(); ++axis)
            if (!std::isfinite(measured[axis]) || std::abs(measured[axis] - expected[axis]) > 0.05f)
                throw std::runtime_error("Native bounds query disagrees at hull " +
                                         std::to_string(i));
    }
    zt::info("collision: baked %zu hulls and %zu shape tags into %zu bytes", hulls.size(),
             tags.size(), output.size());
    return output;
}

} // namespace iw8::havok
