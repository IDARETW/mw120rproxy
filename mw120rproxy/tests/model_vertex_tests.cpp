#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Check(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("D3D failure " + std::to_string(unsigned(result)));
}
static std::string Read(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read shader");
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}
int main(int argc, char** argv) try {
    if (argc != 3)
        return 2;
    auto reference = Read(argv[1]);
    auto source = Read(argv[2]);
    ComPtr<ID3DBlob> candidate, error;
    auto compiled = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main",
                               "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &candidate, &error);
    if (FAILED(compiled) && error)
        puts(static_cast<char*>(error->GetBufferPointer()));
    Check(compiled);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                            D3D11_SDK_VERSION, &device, nullptr, &context));
    constexpr unsigned count = 1024, floats = 17, drawSurface = 3;
    std::mt19937 random(60120);
    std::uniform_real_distribution<float> real(-1, 1);
    auto buffer = [&](const void* data, unsigned size, unsigned bind, unsigned misc,
                      unsigned stride) {
        ComPtr<ID3D11Buffer> result;
        D3D11_BUFFER_DESC desc{size, D3D11_USAGE_DEFAULT, bind, 0, misc, stride};
        D3D11_SUBRESOURCE_DATA initial{data, 0, 0};
        Check(device->CreateBuffer(&desc, data ? &initial : nullptr, &result));
        return result;
    };
    std::vector<ComPtr<ID3D11ShaderResourceView>> resources;
    auto bind = [&](unsigned slot, const void* data, unsigned size, unsigned stride = 0) {
        auto b = buffer(data, size, D3D11_BIND_SHADER_RESOURCE,
                        stride ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED
                               : D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
                        stride);
        ComPtr<ID3D11ShaderResourceView> result;
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
        desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        desc.BufferEx.NumElements = size / (stride ? stride : 4);
        desc.BufferEx.Flags = stride ? 0 : D3D11_BUFFEREX_SRV_FLAG_RAW;
        Check(device->CreateShaderResourceView(b.Get(), &desc, &result));
        auto* view = result.Get();
        context->VSSetShaderResources(slot, 1, &view);
        resources.push_back(result);
        return b;
    };
    std::vector<unsigned> surfaces((0x240000 + 16) / 4);
    surfaces[drawSurface * 4] = 1;
    surfaces[drawSurface * 4 + 1] = 2;
    surfaces[drawSurface * 4 + 2] = 5;
    surfaces[0x240000 / 4 + drawSurface] = 7;
    std::array<unsigned, 32> vertexPages{}, colorPages{};
    for (unsigned i = 0; i < count / 64; ++i)
        vertexPages[5 + i] = 20 - i;
    for (unsigned i = 0; i < count / 256; ++i)
        colorPages[7 + i] = 6 - i;
    std::vector<unsigned> geometry((0x2bc0000 + 32 * 64 * 12) / 4), colors(8 * 256);
    constexpr unsigned halfValues[]{0, 0x3c00, 0xbc00, 0x4000, 0x4400, 0x4900, 0x3800};
    constexpr float uvValues[]{0, 1, -1, 2, 4, 10, .5f};
    for (unsigned i = 0; i < count; ++i) {
        unsigned page = vertexPages[5 + i / 64], vertex = page * 64 + i % 64;
        unsigned position = (page * 512 + (i % 64) * 8) / 4;
        geometry[position] = random();
        geometry[position + 1] = random() & 0x7fffffff;
        unsigned attributes = 0x2bc0000 / 4 + vertex * 3;
        geometry[attributes] = random();
        geometry[attributes + 1] = random();
        geometry[attributes + 2] = halfValues[i % 7] | (halfValues[(i / 7) % 7] << 16);
        colors[colorPages[7 + i / 256] * 256 + i % 256] = random();
    }
    bind(63, surfaces.data(), unsigned(surfaces.size() * 4));
    bind(64, geometry.data(), unsigned(geometry.size() * 4));
    bind(65, vertexPages.data(), sizeof(vertexPages));
    bind(66, colors.data(), unsigned(colors.size() * 4));
    bind(67, colorPages.data(), sizeof(colorPages));
    std::array<float, 24> bounds{};
    bounds[16] = 70;
    bounds[17] = -25;
    bounds[18] = 12;
    bounds[19] = 215;
    bind(75, bounds.data(), sizeof(bounds), 32);
    std::array<unsigned, 12> placements{};
    auto placementBuffer = bind(76, placements.data(), sizeof(placements), 24);
    std::array<unsigned, 4> extras{};
    bind(74, extras.data(), sizeof(extras), 8);
    std::array<float, 24> view{};
    auto viewBuffer = buffer(view.data(), sizeof(view), D3D11_BIND_CONSTANT_BUFFER, 0, 0);
    auto* viewPointer = viewBuffer.Get();
    context->VSSetConstantBuffers(2, 1, &viewPointer);
    std::array<float, 213 * 4> lighting{};
    auto lightingBuffer =
        buffer(lighting.data(), sizeof(lighting), D3D11_BIND_CONSTANT_BUFFER, 0, 0);
    auto* lightingPointer = lightingBuffer.Get();
    context->VSSetConstantBuffers(7, 1, &lightingPointer);
    auto output = buffer(nullptr, count * floats * 4, D3D11_BIND_STREAM_OUTPUT, 0, 0);
    std::array<unsigned, count> indices{};
    for (unsigned i = 0; i < count; ++i)
        indices[i] = (drawSurface << 16) | i;
    auto indexBuffer = buffer(indices.data(), sizeof(indices), D3D11_BIND_INDEX_BUFFER, 0, 0);
    context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    D3D11_BUFFER_DESC stagingDesc{
        count * floats * 4, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ComPtr<ID3D11Buffer> staging;
    Check(device->CreateBuffer(&stagingDesc, nullptr, &staging));
    std::array<D3D11_SO_DECLARATION_ENTRY, 5> entries{{{0, "SV_POSITION", 0, 0, 4, 0},
                                                       {0, "COLOR", 0, 0, 4, 0},
                                                       {0, "TEXCOORDS", 0, 0, 2, 0},
                                                       {0, "NORMAL", 0, 0, 3, 0},
                                                       {0, "TANGENT", 0, 0, 4, 0}}};
    auto capture = [&](const void* code, size_t size) {
        ComPtr<ID3D11VertexShader> vs;
        Check(device->CreateVertexShader(code, size, nullptr, &vs));
        ComPtr<ID3D11GeometryShader> gs;
        unsigned stride = floats * 4;
        Check(device->CreateGeometryShaderWithStreamOutput(
            code, size, entries.data(), unsigned(entries.size()), &stride, 1,
            D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs));
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->GSSetShader(gs.Get(), nullptr, 0);
        auto* target = output.Get();
        unsigned offset = 0;
        context->SOSetTargets(1, &target, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->DrawIndexed(count, 0, 0);
        target = nullptr;
        context->SOSetTargets(1, &target, &offset);
        context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        std::vector<float> values(count * floats);
        std::memcpy(values.data(), mapped.pData, values.size() * 4);
        context->Unmap(staging.Get(), 0);
        return values;
    };
    std::array<unsigned, 4> errors{};
    std::array<float, 4> deltas{};
    constexpr float tolerances[]{.003f, .000002f, 0, .000003f};
    for (unsigned pass = 0; pass < 6; ++pass) {
        std::array<float, 4> quaternion{};
        float length = 0;
        for (auto& value : quaternion) {
            value = real(random);
            length += value * value;
        }
        std::array<unsigned, 4> packed{};
        for (unsigned i = 0; i < 4; ++i)
            packed[i] = unsigned((quaternion[i] / std::sqrt(length) * .5f + .5f) * 65535 + .5f);
        placements[9] = packed[0] | (packed[1] << 16);
        placements[10] = packed[2] | (packed[3] << 16);
        float scale = .5f + pass * .6f;
        std::memcpy(&placements[11], &scale, 4);
        for (unsigned i = 0; i < 16; ++i)
            view[i] = real(random);
        for (unsigned i = 0; i < 3; ++i) {
            placements[6 + i] = unsigned(int(real(random) * 4000 * 4096));
            int origin = int(real(random) * 4000 * 4096);
            std::memcpy(&view[20 + i], &origin, 4);
        }
        context->UpdateSubresource(placementBuffer.Get(), 0, nullptr, placements.data(), 0, 0);
        context->UpdateSubresource(viewBuffer.Get(), 0, nullptr, view.data(), 0, 0);
        auto expected = capture(reference.data(), reference.size());
        auto actual = capture(candidate->GetBufferPointer(), candidate->GetBufferSize());
        for (unsigned vertex = 0; vertex < count; ++vertex) {
            if (expected[vertex * floats + 8] != uvValues[vertex % 7] ||
                expected[vertex * floats + 9] != uvValues[(vertex / 7) % 7])
                throw std::runtime_error("Native shader did not fetch the fixture vertex pages");
            auto rgba = colors[colorPages[7 + vertex / 256] * 256 + vertex % 256];
            for (unsigned channel = 0; channel < 4; ++channel) {
                float value = ((rgba >> (channel * 8)) & 255) / 255.f;
                if (channel < 3)
                    value = value <= .04045f ? value / 12.92f
                                             : std::pow((value + .055f) / 1.055f, 2.4f);
                if (std::abs(expected[vertex * floats + 4 + channel] - value) > .000002f)
                    throw std::runtime_error("Native shader did not fetch the fixture color pages");
            }
        }
        for (unsigned i = 0; i < actual.size(); ++i) {
            unsigned channel = i % floats;
            unsigned group = channel < 4 ? 0 : channel < 8 ? 1 : channel < 10 ? 2 : 3;
            float delta = std::abs(actual[i] - expected[i]);
            deltas[group] = std::max(deltas[group], delta);
            if (!std::isfinite(actual[i]) || !std::isfinite(expected[i]) ||
                delta > tolerances[group]) {
                if (errors[group] < 3)
                    fprintf(stderr, "pass %u vertex %u channel %u: %.9g != %.9g\n", pass,
                            i / floats, channel, actual[i], expected[i]);
                ++errors[group];
            }
        }
    }
    printf("{\"vertices\":%u,\"errors\":[%u,%u,%u,%u],\"max_deltas\":[%.9g,%.9g,%.9g,%.9g]}\n",
           count * 6, errors[0], errors[1], errors[2], errors[3], deltas[0], deltas[1], deltas[2],
           deltas[3]);
    return errors[0] || errors[1] || errors[2] || errors[3] ? 1 : 0;
} catch (const std::exception& error) {
    puts(error.what());
    return 1;
}
