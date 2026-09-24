#include "replay_sunshadow.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace replaysunshadow
{
namespace
{
constexpr std::uint32_t TileResolution = 512;
constexpr std::size_t MaximumBytes = 0x7ffffffcu;

void Store(std::vector<std::uint8_t> &bytes, const std::size_t offset,
           const std::uint32_t value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::uint32_t Read(const std::vector<std::uint8_t> &bytes, const std::size_t offset)
{
    std::uint32_t value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void CheckDepthRange(const float origin, const float inverseSpan)
{
    if (!std::isfinite(origin) || !std::isfinite(inverseSpan) || inverseSpan <= 0.0f)
        throw std::runtime_error("invalid compressed sun-shadow depth range");
}
} // namespace

std::vector<std::uint8_t> EncodeTile(const std::span<const float> depths,
                                    const float origin, const float inverseSpan)
{
    if (depths.size() != TileResolution * TileResolution)
        throw std::runtime_error("compressed sun-shadow tile must contain 512x512 depths");
    CheckDepthRange(origin, inverseSpan);
    for (const float value : depths)
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            throw std::runtime_error("compressed sun-shadow depth is outside [0, 1]");

    struct Node
    {
        std::size_t offset;
        unsigned x, y, size;
    };
    std::vector<std::uint8_t> tile(80);
    Store(tile, 4, TileResolution);
    Store(tile, 8, std::bit_cast<std::uint32_t>(1.0f / TileResolution));
    Store(tile, 16, 0x00020000);
    Store(tile, 24, std::bit_cast<std::uint32_t>(origin));
    Store(tile, 28, std::bit_cast<std::uint32_t>(inverseSpan));
    // Replay's prepass and sunvis start at one of four 12-byte roots. Root +4
    // supplies inherited depth; +8 is not read by either valid-tree traversal.
    std::vector<Node> pending;
    for (unsigned root = 0; root < 4; ++root)
        pending.push_back({32 + 12 * root, (root & 1) * 256u,
                           (root >> 1) * 256u, 256});

    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor)
    {
        const Node node = pending[cursor];
        const unsigned childSize = node.size / 2;
        const std::size_t base = tile.size();
        if (base <= node.offset || base - node.offset > 0x00ffffffu)
            throw std::runtime_error("compressed sun-shadow child offset exceeds 24 bits");
        std::uint32_t packed = static_cast<std::uint32_t>(base - node.offset) << 8;
        for (unsigned child = 0; child < 4; ++child)
        {
            const unsigned x = node.x + (child & 1) * childSize;
            const unsigned y = node.y + (child >> 1) * childSize;
            const auto first = std::bit_cast<std::uint32_t>(depths[y * TileResolution + x]);
            bool uniform = true;
            for (unsigned row = y; row < y + childSize && uniform; ++row)
                for (unsigned col = x; col < x + childSize; ++col)
                    if (std::bit_cast<std::uint32_t>(depths[row * TileResolution + col]) != first)
                    {
                        uniform = false;
                        break;
                    }

            // Type 1 retains the root's inherited zero. Type 3 owns an exact
            // float leaf. Only positive zero can be omitted as type 0.
            const unsigned type = uniform ? (first == 0 ? 0 : 3) : 1;
            packed |= type << (2 * child);
            if (type)
            {
                const std::size_t offset = tile.size();
                tile.resize(offset + 4);
                if (type == 1)
                    pending.push_back({offset, x, y, childSize});
                else
                    Store(tile, offset, first);
            }
        }
        Store(tile, node.offset, packed);
    }
    Store(tile, 0, static_cast<std::uint32_t>(tile.size()));
    Store(tile, 12, static_cast<std::uint32_t>(tile.size()));
    return tile;
}

std::vector<std::uint8_t> EncodeForest(
    const std::uint32_t resolution, const std::uint16_t width, const std::uint16_t height,
    const float origin, const float inverseSpan,
    const std::span<const std::vector<std::uint8_t>> tiles)
{
    CheckDepthRange(origin, inverseSpan);
    if (resolution < TileResolution || !std::has_single_bit(resolution) ||
        !width || !height || std::uint32_t(width) > resolution / TileResolution ||
        std::uint32_t(height) > resolution / TileResolution ||
        tiles.size() != std::size_t(width) * height || tiles.size() > (MaximumBytes - 32) / 4)
        throw std::runtime_error("invalid compressed sun-shadow forest dimensions");

    std::size_t size = 32 + tiles.size() * 4;
    for (const auto &tile : tiles)
    {
        if (tile.empty())
            continue;
        if (tile.size() < 80 || (tile.size() & 3) || Read(tile, 0) != tile.size() ||
            Read(tile, 12) != tile.size() || Read(tile, 4) != TileResolution ||
            Read(tile, 8) != std::bit_cast<std::uint32_t>(1.0f / TileResolution) ||
            Read(tile, 16) != 0x00020000)
            throw std::runtime_error("invalid compressed sun-shadow mini-tile header");
        CheckDepthRange(std::bit_cast<float>(Read(tile, 24)),
                        std::bit_cast<float>(Read(tile, 28)));
        if (tile.size() > MaximumBytes - size)
            throw std::runtime_error("compressed sun-shadow forest exceeds 31-bit pointers");
        size += tile.size();
    }

    std::vector<std::uint8_t> forest(32 + tiles.size() * 4);
    forest.reserve(size);
    Store(forest, 0, static_cast<std::uint32_t>(size));
    Store(forest, 4, resolution);
    Store(forest, 8, std::bit_cast<std::uint32_t>(1.0f / resolution));
    Store(forest, 12, static_cast<std::uint32_t>(size));
    Store(forest, 16, 0x00030009);
    Store(forest, 20, std::uint32_t(width) | (std::uint32_t(height) << 16));
    Store(forest, 24, std::bit_cast<std::uint32_t>(origin));
    Store(forest, 28, std::bit_cast<std::uint32_t>(inverseSpan));
    for (std::size_t cell = 0; cell < tiles.size(); ++cell)
        if (!tiles[cell].empty())
        {
            Store(forest, 32 + 4 * cell, 0x80000000u | static_cast<std::uint32_t>(forest.size()));
            forest.insert(forest.end(), tiles[cell].begin(), tiles[cell].end());
        }
    return forest;
}

Data Bake(const Scene &scene, const std::array<float, 3> &sunDirection,
          const replaybounds::Bounds &receiverBounds)
{
    using Microsoft::WRL::ComPtr;
    using Vec3 = std::array<float, 3>;
    const auto dot = [](const Vec3 &a, const Vec3 &b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto cross = [](const Vec3 &a, const Vec3 &b) -> Vec3 {
        return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
    };
    const auto check = [](const HRESULT result, const char *operation) {
        if (FAILED(result))
            throw std::runtime_error(std::string("sun-shadow ") + operation +
                                     " failed: " + std::to_string(static_cast<unsigned>(result)));
    };
    const float sunLength = dot(sunDirection, sunDirection);
    if (!std::isfinite(sunLength) || std::abs(sunLength - 1.0f) > 0.001f)
        throw std::runtime_error("compressed sun-shadow direction must be a unit vector");
    if (scene.surfaces.empty())
        return {};

    // Replay 0xEB9090 -> 0x198E4B0. Keep the stored direction unchanged:
    // 0xEB8230 compares it with the active sun with a 0.001 component tolerance.
    Vec3 right = cross(std::abs(sunDirection[2]) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 0, 1},
                       sunDirection);
    const float rightLength = std::sqrt(dot(right, right));
    for (float &component : right)
        component /= rightLength;
    const Vec3 up = cross(sunDirection, right);
    std::vector<Surface> projected = scene.surfaces;
    Vec3 minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY};
    for (auto &surface : projected)
    {
        if (surface.material >= scene.materials.size() || surface.vertices.empty() ||
            surface.vertices.size() % 3)
            throw std::runtime_error("invalid compressed sun-shadow caster surface");
        for (auto &vertex : surface.vertices)
        {
            for (const float value : vertex.position)
                if (!std::isfinite(value) || std::abs(value) > 100000.0f)
                    throw std::runtime_error("invalid compressed sun-shadow caster position");
            if (!std::isfinite(vertex.uv[0]) || !std::isfinite(vertex.uv[1]) ||
                !std::isfinite(vertex.alpha) || vertex.alpha < 0 || vertex.alpha > 1)
                throw std::runtime_error("invalid compressed sun-shadow caster attributes");
            vertex.position = {dot(right, vertex.position), -dot(up, vertex.position),
                                dot(sunDirection, vertex.position)};
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
                maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
            }
        }
    }
    constexpr float sampleSize = 1.0f;
    constexpr float tileWorldSize = TileResolution * sampleSize;
    const float startX = std::floor(minimum[0] / tileWorldSize) * tileWorldSize;
    const float startY = std::floor(minimum[1] / tileWorldSize) * tileWorldSize;
    const auto width = static_cast<std::uint32_t>(std::max(1.0f,
        std::ceil((maximum[0] - startX) / tileWorldSize)));
    const auto height = static_cast<std::uint32_t>(std::max(1.0f,
        std::ceil((maximum[1] - startY) / tileWorldSize)));
    if (width > UINT16_MAX || height > UINT16_MAX)
        throw std::runtime_error("compressed sun-shadow crop exceeds native dimensions");
    Data result;
    auto &parameters = result.parameters;
    parameters.sunDirection = sunDirection;
    parameters.resolution = std::bit_ceil(std::max(width, height)) * TileResolution;
    parameters.sampleSize = sampleSize;
    parameters.centerX = parameters.resolution * sampleSize * 0.5f + startX;
    parameters.centerY = -startY - parameters.resolution * sampleSize * 0.5f;
    parameters.forestSize = width * height;
    parameters.reserved = width << 16;
    // The shader's outer near-plane guard makes receivers below the depth
    // range unconditionally lit. Include the physical world's receiver
    // bounds, not just eligible casters: a non-casting floor can be below
    // every caster and must still receive their shadows.
    float receiverRadius = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(receiverBounds.midpoint[axis]) ||
            !std::isfinite(receiverBounds.halfSize[axis]) ||
            receiverBounds.halfSize[axis] < 0 ||
            std::abs(receiverBounds.midpoint[axis]) + receiverBounds.halfSize[axis] > 100001)
            throw std::runtime_error("invalid compressed sun-shadow receiver bounds");
        receiverRadius += std::abs(sunDirection[axis]) * receiverBounds.halfSize[axis];
    }
    const float receiverCenter = dot(sunDirection, receiverBounds.midpoint);
    minimum[2] = std::min(minimum[2], receiverCenter - receiverRadius);
    maximum[2] = std::max(maximum[2], receiverCenter + receiverRadius);
    // Padding keeps occupied samples away from the zero/no-caster sentinel.
    // This changes the normalization range, not the world-space caster depth:
    // receivers and occluders use the same origin and inverse span.
    const float origin = 1.0f - minimum[2];
    const float inverseSpan = 1.0f / (maximum[2] - minimum[2] + 2.0f);
    std::vector<std::vector<std::size_t>> candidates(std::size_t(width) * height);
    for (std::size_t index = 0; index < projected.size(); ++index)
    {
        auto &surface = projected[index];
        float minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        for (auto &vertex : surface.vertices)
        {
            vertex.position[0] = (vertex.position[0] - startX) / sampleSize;
            vertex.position[1] = (vertex.position[1] - startY) / sampleSize;
            vertex.position[2] = (vertex.position[2] + origin) * inverseSpan;
            minX = std::min(minX, vertex.position[0]);
            minY = std::min(minY, vertex.position[1]);
            maxX = std::max(maxX, vertex.position[0]);
            maxY = std::max(maxY, vertex.position[1]);
        }
        const auto left = std::min(width - 1, static_cast<unsigned>(minX / TileResolution));
        const auto top = std::min(height - 1, static_cast<unsigned>(minY / TileResolution));
        const auto lastX = std::min(width - 1, static_cast<unsigned>(maxX / TileResolution));
        const auto lastY = std::min(height - 1, static_cast<unsigned>(maxY / TileResolution));
        for (unsigned y = top; y <= lastY; ++y)
            for (unsigned x = left; x <= lastX; ++x)
                candidates[y * width + x].push_back(index);
    }

    constexpr char shaderSource[] = R"(
cbuffer Material : register(b0) { uint alphaTest; uint atlasTile; float2 imageSize; };
Texture2D<float> alphaImage : register(t0);
SamplerState alphaSampler : register(s0);
struct Input { float3 position : POSITION; float2 uv : TEXCOORD; float alpha : COLOR; };
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD; float alpha : COLOR; };
Output vs(Input input) {
    Output output;
    output.position = float4(input.position.x / 256.0 - 1.0,
                             1.0 - input.position.y / 256.0, input.position.z, 1.0);
    output.uv = input.uv;
    output.alpha = input.alpha;
    return output;
}
void ps(Output input) {
    float alpha;
    if (atlasTile) {
        float footprint = max(length(ddx(input.uv) * imageSize),
                              length(ddy(input.uv) * imageSize));
        float lod = clamp(log2(max(1.0, footprint)), 0, log2(imageSize.x) - 2);
        float border = 0.5 * exp2(ceil(lod));
        float2 uv = clamp(0.5 + frac(input.uv) * (imageSize - 1),
                          border, imageSize - border) / imageSize;
        alpha = alphaImage.SampleLevel(alphaSampler, uv, lod);
    } else {
        alpha = alphaImage.Sample(alphaSampler, input.uv);
    }
    alpha *= input.alpha;
    if (alphaTest == 2) { if (alpha <= 0) discard; }
    else if (alphaTest == 3) { if (alpha >= 0.5) discard; }
    else clip(alpha - 0.5);
})";
    const auto compile = [&](const char *entry, const char *target) {
        ComPtr<ID3DBlob> code, errors;
        const HRESULT status = D3DCompile(shaderSource, sizeof(shaderSource) - 1, nullptr,
            nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(status))
            throw std::runtime_error(errors
                ? std::string(static_cast<const char *>(errors->GetBufferPointer()),
                              errors->GetBufferSize()) : "sun-shadow shader compilation failed");
        return code;
    };
    const auto vertexCode = compile("vs", "vs_5_0"), pixelCode = compile("ps", "ps_5_0");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                            D3D11_SDK_VERSION, &device, nullptr, &context), "create WARP device");
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                                     nullptr, &vertexShader), "create vertex shader");
    check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
                                    nullptr, &pixelShader), "create pixel shader");
    const std::array<D3D11_INPUT_ELEMENT_DESC, 3> elements{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}}};
    static_assert(sizeof(Vertex) == 24);
    ComPtr<ID3D11InputLayout> inputLayout;
    check(device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()),
                                    vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                                    &inputLayout), "create caster input layout");
    context->IASetInputLayout(inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader.Get(), nullptr, 0);
    std::array<ComPtr<ID3D11RasterizerState>, 3> rasterizers;
    for (unsigned index = 0; index < rasterizers.size(); ++index)
    {
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = std::array{D3D11_CULL_NONE, D3D11_CULL_BACK, D3D11_CULL_FRONT}[index];
        raster.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&raster, &rasterizers[index]), "create rasterizer");
    }
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    ComPtr<ID3D11DepthStencilState> depthState;
    check(device->CreateDepthStencilState(&depth, &depthState), "create depth state");
    context->OMSetDepthStencilState(depthState.Get(), 0);
    const D3D11_VIEWPORT viewport{0, 0, float(TileResolution), float(TileResolution), 0, 1};
    context->RSSetViewports(1, &viewport);
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = texture.Height = TileResolution;
    texture.MipLevels = texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_R32_TYPELESS;
    texture.SampleDesc.Count = 1;
    texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> target, readback;
    check(device->CreateTexture2D(&texture, nullptr, &target), "create depth image");
    D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDescription{};
    depthViewDescription.Format = DXGI_FORMAT_D32_FLOAT;
    depthViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> depthView;
    check(device->CreateDepthStencilView(target.Get(), &depthViewDescription, &depthView),
          "create depth view");
    texture.Usage = D3D11_USAGE_STAGING;
    texture.BindFlags = 0;
    texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&texture, nullptr, &readback), "create readback image");
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> alphaSampler;
    check(device->CreateSamplerState(&sampler, &alphaSampler), "create alpha sampler");
    context->PSSetSamplers(0, 1, alphaSampler.GetAddressOf());
    std::vector<ComPtr<ID3D11Buffer>> constants(scene.materials.size());
    std::vector<ComPtr<ID3D11ShaderResourceView>> alphaViews(scene.materials.size());
    for (std::size_t index = 0; index < scene.materials.size(); ++index)
    {
        const auto &material = scene.materials[index];
        if (material.cullMode > 2 || material.alphaTest > 3)
            throw std::runtime_error("invalid compressed sun-shadow material state");
        if (!material.alphaTest)
            continue;
        if (!material.width || !material.height || material.width > 16384 ||
            material.height > 16384 || material.alpha.size() != std::size_t(material.width) * material.height ||
            (material.atlasTile && (material.width != material.height || material.width < 4)))
            throw std::runtime_error("invalid compressed sun-shadow alpha image");
        const std::array<std::uint32_t, 4> values{material.alphaTest, material.atlasTile ? 1u : 0u,
            std::bit_cast<std::uint32_t>(float(material.width)),
            std::bit_cast<std::uint32_t>(float(material.height))};
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(values);
        buffer.Usage = D3D11_USAGE_IMMUTABLE;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA constantData{values.data(), 0, 0};
        check(device->CreateBuffer(&buffer, &constantData, &constants[index]), "create alpha constants");
        std::vector<std::vector<std::uint8_t>> levels{material.alpha};
        std::vector<D3D11_SUBRESOURCE_DATA> subresources;
        unsigned mipWidth = material.width, mipHeight = material.height;
        for (;;)
        {
            subresources.push_back({levels.back().data(), mipWidth, 0});
            if ((mipWidth == 1 && mipHeight == 1) ||
                (material.atlasTile && mipWidth == 4 && mipHeight == 4))
                break;
            const unsigned nextWidth = std::max(1u, mipWidth / 2);
            const unsigned nextHeight = std::max(1u, mipHeight / 2);
            std::vector<std::uint8_t> next(std::size_t(nextWidth) * nextHeight);
            for (unsigned y = 0; y < nextHeight; ++y)
                for (unsigned x = 0; x < nextWidth; ++x)
                {
                    unsigned sum = 0;
                    for (unsigned dy = 0; dy < 2; ++dy)
                        for (unsigned dx = 0; dx < 2; ++dx)
                            sum += levels.back()[std::min(y * 2 + dy, mipHeight - 1) * mipWidth +
                                                  std::min(x * 2 + dx, mipWidth - 1)];
                    next[y * nextWidth + x] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            levels.push_back(std::move(next));
            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }
        D3D11_TEXTURE2D_DESC alphaTexture{};
        alphaTexture.Width = material.width;
        alphaTexture.Height = material.height;
        alphaTexture.MipLevels = static_cast<UINT>(levels.size());
        alphaTexture.ArraySize = 1;
        alphaTexture.Format = DXGI_FORMAT_R8_UNORM;
        alphaTexture.SampleDesc.Count = 1;
        alphaTexture.Usage = D3D11_USAGE_IMMUTABLE;
        alphaTexture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> alphaImage;
        check(device->CreateTexture2D(&alphaTexture, subresources.data(), &alphaImage), "create alpha image");
        check(device->CreateShaderResourceView(alphaImage.Get(), nullptr, &alphaViews[index]),
              "create alpha image view");
    }

    std::vector<std::vector<std::uint8_t>> tiles(std::size_t(width) * height);
    std::vector<float> pixels(TileResolution * TileResolution);
    std::vector<Vertex> vertices;
    std::size_t encodedSize = 32 + tiles.size() * 4;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const auto cell = y * width + x;
            if (candidates[cell].empty())
                continue;
            context->OMSetRenderTargets(0, nullptr, depthView.Get());
            context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH, 0, 0);
            for (const auto index : candidates[cell])
            {
                const auto &surface = projected[index];
                const auto &material = scene.materials[surface.material];
                vertices.clear();
                for (std::size_t first = 0; first < surface.vertices.size(); first += 3)
                {
                    std::array<Vertex, 3> triangle;
                    float minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
                    for (unsigned corner = 0; corner < 3; ++corner)
                    {
                        auto &vertex = triangle[corner];
                        vertex = surface.vertices[first + corner];
                        vertex.position[0] -= float(x * TileResolution);
                        vertex.position[1] -= float(y * TileResolution);
                        minX = std::min(minX, vertex.position[0]);
                        maxX = std::max(maxX, vertex.position[0]);
                        minY = std::min(minY, vertex.position[1]);
                        maxY = std::max(maxY, vertex.position[1]);
                    }
                    if (maxX >= 0 && maxY >= 0 && minX < TileResolution && minY < TileResolution)
                        vertices.insert(vertices.end(), triangle.begin(), triangle.end());
                }
                if (vertices.empty())
                    continue;
                if (vertices.size() > UINT32_MAX / sizeof(Vertex))
                    throw std::runtime_error("compressed sun-shadow vertex buffer exceeds native size");
                D3D11_BUFFER_DESC buffer{};
                buffer.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
                buffer.Usage = D3D11_USAGE_IMMUTABLE;
                buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                const D3D11_SUBRESOURCE_DATA vertexData{vertices.data(), 0, 0};
                ComPtr<ID3D11Buffer> vertexBuffer;
                check(device->CreateBuffer(&buffer, &vertexData, &vertexBuffer), "create caster vertices");
                const UINT stride = sizeof(Vertex), offset = 0;
                context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
                context->RSSetState(rasterizers[material.cullMode].Get());
                context->PSSetShader(material.alphaTest ? pixelShader.Get() : nullptr, nullptr, 0);
                context->PSSetConstantBuffers(0, 1, constants[surface.material].GetAddressOf());
                context->PSSetShaderResources(0, 1, alphaViews[surface.material].GetAddressOf());
                context->Draw(static_cast<UINT>(vertices.size()), 0);
            }
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context->CopyResource(readback.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "read caster depth");
            for (unsigned row = 0; row < TileResolution; ++row)
                std::memcpy(pixels.data() + row * TileResolution,
                            static_cast<const std::uint8_t *>(mapped.pData) + row * mapped.RowPitch,
                            TileResolution * sizeof(float));
            context->Unmap(readback.Get(), 0);
            if (std::none_of(pixels.begin(), pixels.end(), [](float value) { return value != 0; }))
                continue;
            tiles[cell] = EncodeTile(pixels, origin, inverseSpan);
            if (tiles[cell].size() > MaximumBytes - encodedSize)
                throw std::runtime_error("compressed sun-shadow bake exceeds 31-bit pointers");
            encodedSize += tiles[cell].size();
        }
    result.bytes = EncodeForest(parameters.resolution, static_cast<std::uint16_t>(width),
                                static_cast<std::uint16_t>(height), origin, inverseSpan, tiles);
    return result;
}
} // namespace replaysunshadow
