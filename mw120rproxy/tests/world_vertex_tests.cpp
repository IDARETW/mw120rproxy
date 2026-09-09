#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
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
static ComPtr<ID3DBlob> Compile(const std::string& text, const char* target) {
    ComPtr<ID3DBlob> code, error;
    HRESULT result = D3DCompile(text.data(), text.size(), nullptr, nullptr, nullptr, "main", target,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    if (FAILED(result) && error)
        puts((char*)error->GetBufferPointer());
    Check(result);
    return code;
}
int main(int argc, char** argv) try {
    if (argc != 3)
        return 2;
    auto reference = Read(argv[1]);
    auto candidate = Compile(Read(argv[2]), "vs_5_0");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                            D3D11_SDK_VERSION, &device, nullptr, &context));
    constexpr unsigned count = 4096, floats = 28;
    std::mt19937 random(74021);
    std::uniform_real_distribution<float> real(-8000, 8000);
    std::vector<float> positions(1 + count * 3);
    std::vector<unsigned> aux(1 + count * 18);
    const unsigned normalOffset = 4, uvOffset = (1 + count) * 4, lmOffset = (1 + count * 3) * 4,
                   colorOffset = (1 + count * 5) * 4, layeredUvOffset = (1 + count * 6) * 4,
                   parameterUvOffset = (1 + count * 10) * 4;
    const float materialParameters[]{.4f, 3.f, 1.7f, .875f};
    for (unsigned i = 0; i < count; ++i) {
        for (unsigned axis = 0; axis < 3; ++axis)
            positions[1 + i * 3 + axis] = real(random);
        aux[1 + i] = random();
        for (unsigned j = 0; j < 2; ++j) {
            float uv = real(random) / 1024;
            std::memcpy(&aux[uvOffset / 4 + i * 2 + j], &uv, 4);
            uv = (real(random) + 8000) / 16000;
            std::memcpy(&aux[lmOffset / 4 + i * 2 + j], &uv, 4);
        }
        aux[colorOffset / 4 + i] = random();
        for (unsigned j = 0; j < 2; ++j) {
            aux[layeredUvOffset / 4 + i * 4 + j] = aux[uvOffset / 4 + i * 2 + j];
            float metadata = j ? 4.f : 200.f;
            std::memcpy(&aux[layeredUvOffset / 4 + i * 4 + 2 + j], &metadata, 4);
        }
        std::memcpy(&aux[parameterUvOffset / 4 + i * 8], &aux[layeredUvOffset / 4 + i * 4], 16);
        std::memcpy(&aux[parameterUvOffset / 4 + i * 8 + 4], materialParameters, 16);
    }
    auto buffer = [&](const void* data, unsigned size, unsigned bind, unsigned misc,
                      unsigned stride) {
        ComPtr<ID3D11Buffer> result;
        D3D11_BUFFER_DESC desc{size, D3D11_USAGE_DEFAULT, bind, 0, misc, stride};
        D3D11_SUBRESOURCE_DATA initial{data, 0, 0};
        Check(device->CreateBuffer(&desc, data ? &initial : nullptr, &result));
        return result;
    };
    auto resource = [&](ID3D11Buffer* b, unsigned elements, bool structured) {
        ComPtr<ID3D11ShaderResourceView> result;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = structured ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        view.BufferEx.NumElements = elements;
        view.BufferEx.Flags = structured ? 0 : D3D11_BUFFEREX_SRV_FLAG_RAW;
        Check(device->CreateShaderResourceView(b, &view, &result));
        return result;
    };
    auto positionBuffer =
        buffer(positions.data(), unsigned(positions.size() * 4), D3D11_BIND_SHADER_RESOURCE,
               D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0);
    auto auxBuffer = buffer(aux.data(), unsigned(aux.size() * 4), D3D11_BIND_SHADER_RESOURCE,
                            D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0);
    std::array<unsigned, 44> surface{};
    surface[22 + 1] = 1;
    surface[22 + 2] = 4;
    surface[22 + 3] = normalOffset;
    surface[22 + 4] = lmOffset;
    surface[22 + 5] = colorOffset;
    surface[22 + 6] = uvOffset;
    auto surfaceBuffer = buffer(surface.data(), sizeof(surface), D3D11_BIND_SHADER_RESOURCE,
                                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 88);
    auto positionView = resource(positionBuffer.Get(), unsigned(positions.size()), false);
    auto auxView = resource(auxBuffer.Get(), unsigned(aux.size()), false);
    auto surfaceView = resource(surfaceBuffer.Get(), 2, true);
    ID3D11ShaderResourceView* views[]{surfaceView.Get(), positionView.Get(), auxView.Get()};
    context->VSSetShaderResources(13, 3, views);
    std::array<unsigned, 4> index{1};
    auto surfaceConstant = buffer(index.data(), 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0);
    ID3D11Buffer* sc = surfaceConstant.Get();
    context->VSSetConstantBuffers(9, 1, &sc);
    std::array<float, 24> view{};
    auto viewConstant = buffer(view.data(), sizeof(view), D3D11_BIND_CONSTANT_BUFFER, 0, 0);
    ID3D11Buffer* vc = viewConstant.Get();
    context->VSSetConstantBuffers(2, 1, &vc);
    auto output = buffer(nullptr, count * floats * 4, D3D11_BIND_STREAM_OUTPUT, 0, 0);
    D3D11_BUFFER_DESC stagingDesc{
        count * floats * 4, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ComPtr<ID3D11Buffer> staging;
    Check(device->CreateBuffer(&stagingDesc, nullptr, &staging));
    std::array<D3D11_SO_DECLARATION_ENTRY, 8> entries{{{0, "SV_POSITION", 0, 0, 4, 0},
                                                       {0, "TEXCOORDS", 0, 0, 4, 0},
                                                       {0, "LMAPCOORDS", 0, 0, 2, 0},
                                                       {0, "NORMAL", 0, 0, 3, 0},
                                                       {0, "TANGENT", 0, 0, 4, 0},
                                                       {0, "COLOR", 0, 0, 4, 0},
                                                       {0, "MATERIALPARMS", 0, 0, 4, 0},
                                                       {0, "WORLDPOS", 0, 0, 3, 0}}};
    auto capture = [&](const void* code, size_t size, bool colors) {
        ComPtr<ID3D11VertexShader> vs;
        Check(device->CreateVertexShader(code, size, nullptr, &vs));
        ComPtr<ID3D11GeometryShader> gs;
        unsigned stride = (colors ? floats : 17) * 4;
        Check(device->CreateGeometryShaderWithStreamOutput(
            code, size, entries.data(), colors ? 8 : 5, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM,
            nullptr, &gs));
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->GSSetShader(gs.Get(), nullptr, 0);
        ID3D11Buffer* target = output.Get();
        unsigned offset = 0;
        context->SOSetTargets(1, &target, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->Draw(count, 0);
        target = nullptr;
        context->SOSetTargets(1, &target, &offset);
        context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        std::vector<float> values(count * (colors ? floats : 17));
        std::memcpy(values.data(), mapped.pData, values.size() * 4);
        context->Unmap(staging.Get(), 0);
        return values;
    };
    unsigned positionErrors = 0, attributeErrors = 0, colorErrors = 0;
    float largestNormalDelta = 0;
    for (unsigned pass = 0; pass < 6; ++pass) {
        for (unsigned i = 0; i < 16; ++i)
            view[i] = real(random) / 8000;
        for (unsigned i = 0; i < 3; ++i) {
            int origin = int(real(random) * 4096);
            std::memcpy(&view[20 + i], &origin, 4);
        }
        // The last two cases verify the native optional-color zero-offset fallback.
        surface[22 + 5] = pass >= 4 ? 0 : colorOffset;
        const bool layered = pass == 2 || pass == 3;
        const bool parameters = pass == 3;
        surface[22 + 1] = parameters ? 4 : layered ? 2 : 1;
        surface[22 + 6] = parameters ? parameterUvOffset : layered ? layeredUvOffset : uvOffset;
        context->UpdateSubresource(surfaceBuffer.Get(), 0, nullptr, surface.data(), 0, 0);
        context->UpdateSubresource(viewConstant.Get(), 0, nullptr, view.data(), 0, 0);
        auto expected = capture(reference.data(), reference.size(), false);
        auto actual = capture(candidate->GetBufferPointer(), candidate->GetBufferSize(), true);
        for (unsigned i = 0; i < count; ++i) {
            for (unsigned j = 0; j < 17; ++j) {
                float a = actual[i * floats + j], b = expected[i * 17 + j];
                if (layered && (j == 6 || j == 7))
                    b = j == 6 ? 200.f : 4.f;
                if (j < 10 && std::memcmp(&a, &b, 4)) {
                    if (j < 4)
                        ++positionErrors;
                    else
                        ++attributeErrors;
                }
                if (j >= 10) {
                    largestNormalDelta = std::max(largestNormalDelta, std::abs(a - b));
                    if (std::abs(a - b) > 0.000002f)
                        ++attributeErrors;
                }
            }
            for (unsigned j = 0; j < 4; ++j) {
                float expectedColor =
                    pass >= 4 ? 1.f : ((aux[colorOffset / 4 + i] >> (j * 8)) & 255) / 255.f;
                if (std::abs(actual[i * floats + 17 + j] - expectedColor) > 0.000001f)
                    ++colorErrors;
                const float fallback[]{.8f, 4.f, 2.5f, .625f};
                if (actual[i * floats + 21 + j] !=
                    (parameters ? materialParameters[j] : fallback[j]))
                    ++attributeErrors;
                if (j < 3) {
                    int origin;
                    std::memcpy(&origin, &view[20 + j], 4);
                    float expectedPosition = positions[1 + i * 3 + j] + float(-origin) / 4096.f;
                    if (actual[i * floats + 25 + j] != expectedPosition)
                        ++attributeErrors;
                }
            }
        }
    }
    printf(
        "{\"vertices\":%u,\"position_bit_errors\":%u,\"attribute_errors\":%u,\"color_errors\":%u,\"max_normal_delta\":%.9g}\n",
        count * 6, positionErrors, attributeErrors, colorErrors, largestNormalDelta);
    return positionErrors || attributeErrors || colorErrors ? 1 : 0;
} catch (const std::exception& error) {
    puts(error.what());
    return 1;
}
