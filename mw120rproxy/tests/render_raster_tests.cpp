#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdio>
#include <stdexcept>
#include <array>
#include <cmath>
using Microsoft::WRL::ComPtr;
void Check(HRESULT h) {
    if (FAILED(h))
        throw std::runtime_error("D3D error " + std::to_string((unsigned)h));
}
void Replace(std::string& text, const std::string& old, const std::string& value) {
    size_t p = 0;
    while ((p = text.find(old, p)) != std::string::npos) {
        text.replace(p, old.size(), value);
        p += value.size();
    }
}
ComPtr<ID3DBlob> Compile(const std::string& source, const char* target) {
    ComPtr<ID3DBlob> code, error;
    auto h = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", target,
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    if (FAILED(h)) {
        if (error)
            puts((char*)error->GetBufferPointer());
        Check(h);
    }
    return code;
}
int main(int argc, char** argv) try {
    if (argc < 3)
        return 2;
    const std::string mode = argv[2];
    const bool baked = mode == "baked" || mode == "baked_half";
    const bool live =
        mode == "shadow" || mode == "ao" || mode == "night" || mode == "glass" || baked;
    std::ifstream file(argv[1]);
    std::stringstream input;
    input << file.rdbuf();
    std::string source = input.str();
    if (mode == "baked_half" || mode == "sky_half")
        source = "#define MAP_INDIRECT_GAIN 0.5\n#define MAP_UNBAKED_GAIN 0.5\n" + source;
    Replace(source, "ATLAS_COLUMNS", "4");
    Replace(source, "SUN_DIRECTION", "float3(0,0,1)");
    Replace(source, "SUN_COLOR", "float3(1,1,1)");
    auto psCode = Compile(source, "ps_5_0");
    auto vsCode = Compile(R"(
cbuffer Fixture : register(b0) {
    float2 metadata;
};
struct Output {
    float4 position : SV_POSITION;
    float4 uv : TEXCOORDS0;
    float2 lightmapUV : LMAPCOORDS0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
};
Output main(uint id : SV_VertexID) {
    uint index[6] = {0, 1, 2, 2, 1, 3};
    float2 p[4] = {float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1)};
    float w[4] = {.17, 7.13, 2.7, 1.4};
    uint i = index[id];
    Output o;
    o.position = float4(p[i] * w[i], .5 * w[i], w[i]);
    o.uv = float4(p[i] * .5 + .5, 0, 0);
    o.lightmapUV = metadata;
    o.normal = float3(0, 0, 1);
    o.tangent = float4(1, 0, 0, 1);
    return o;
}
 )",
                          "vs_5_0");
    ComPtr<ID3D11Device> d;
    ComPtr<ID3D11DeviceContext> c;
    D3D_FEATURE_LEVEL level;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                            D3D11_SDK_VERSION, &d, &level, &c));
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    Check(d->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs));
    Check(d->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps));
    std::vector<unsigned> pixels(4096 * 4096);
    for (unsigned y = 0; y < 4096; ++y)
        for (unsigned x = 0; x < 4096; ++x) {
            unsigned tile = x / 1024 + 4 * (y / 1024);
            pixels[y * 4096 + x] = tile == 9 ? 0xFFE6331A : 0xFF1A33E6;
            if (baked)
                pixels[y * 4096 + x] = tile == 9 ? 0xFFC0A080 : 0x80665040;
        }
    D3D11_TEXTURE2D_DESC tex{4096,
                             4096,
                             1,
                             1,
                             DXGI_FORMAT_R8G8B8A8_UNORM,
                             {1, 0},
                             D3D11_USAGE_IMMUTABLE,
                             D3D11_BIND_SHADER_RESOURCE,
                             0,
                             0};
    D3D11_SUBRESOURCE_DATA data{pixels.data(), 4096 * 4, 0};
    if (baked)
        tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    ComPtr<ID3D11Texture2D> atlas;
    Check(d->CreateTexture2D(&tex, &data, &atlas));
    ComPtr<ID3D11ShaderResourceView> srv;
    Check(d->CreateShaderResourceView(atlas.Get(), nullptr, &srv));
    D3D11_TEXTURE2D_DESC desc{256,
                              256,
                              1,
                              1,
                              DXGI_FORMAT_R8G8B8A8_UNORM,
                              {1, 0},
                              D3D11_USAGE_DEFAULT,
                              D3D11_BIND_RENDER_TARGET,
                              0,
                              0};
    ComPtr<ID3D11Texture2D> target, read;
    Check(d->CreateTexture2D(&desc, nullptr, &target));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(d->CreateTexture2D(&desc, nullptr, &read));
    ComPtr<ID3D11RenderTargetView> rtv;
    Check(d->CreateRenderTargetView(target.Get(), nullptr, &rtv));
    std::vector<float> constants(58 * 4);
    constants[42 * 4] = 1;
    constants[56 * 4 + 2] = 1;
    constants[56 * 4 + 3] = mode == "night" ? 0.f : 1.f;
    constants[57 * 4] = .1f;
    constants[57 * 4 + 1] = .15f;
    constants[57 * 4 + 2] = .2f;
    constants[47 * 4] = mode == "ao" ? 1.f : 0.f;
    constants[47 * 4 + 1] = 1;
    D3D11_BUFFER_DESC cb{
        unsigned(constants.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA cbd{constants.data(), 0, 0};
    ComPtr<ID3D11Buffer> lighting;
    Check(d->CreateBuffer(&cb, &cbd, &lighting));
    const bool biased = mode == "biased" || mode == "sky_half" || live;
    float meta[]{9 + (biased ? .25f : 0),
                 (live ? (mode == "glass" ? 2.f : 0.f) : 1.f) + (biased ? .25f : 0), 0, 0};
    if (baked) {
        meta[0] = 9.25f + .25f * .125f;
        meta[1] = 4.25f + .25f * .125f;
    }
    cb.ByteWidth = 16;
    cbd.pSysMem = meta;
    ComPtr<ID3D11Buffer> metadata;
    Check(d->CreateBuffer(&cb, &cbd, &metadata));
    std::array<float, 32> viewConstants{};
    viewConstants[6 * 4 + 2] = viewConstants[6 * 4 + 3] = 1.f / 256;
    viewConstants[7 * 4] = 1;
    cb.ByteWidth = sizeof(viewConstants);
    cbd.pSysMem = viewConstants.data();
    ComPtr<ID3D11Buffer> view;
    Check(d->CreateBuffer(&cb, &cbd, &view));
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> masks;
    for (unsigned index = 0; index < 2; ++index) {
        std::vector<std::array<float, 4>> mask(256 * 256);
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned x = 0; x < 256; ++x) {
                float value = 1;
                if (!index && (mode == "shadow" || mode == "glass"))
                    value = x < 128 ? 0.f : 1.f;
                if (index && mode == "ao")
                    value = x < 128 ? .5f : 1.f;
                mask[y * 256 + x] = {value, .37f, .63f, 0.f};
            }
        D3D11_TEXTURE2D_DESC md{256,
                                256,
                                1,
                                1,
                                DXGI_FORMAT_R32G32B32A32_FLOAT,
                                {1, 0},
                                D3D11_USAGE_IMMUTABLE,
                                D3D11_BIND_SHADER_RESOURCE,
                                0,
                                0};
        D3D11_SUBRESOURCE_DATA ms{mask.data(), 256 * 16, 0};
        ComPtr<ID3D11Texture2D> texture;
        Check(d->CreateTexture2D(&md, &ms, &texture));
        Check(d->CreateShaderResourceView(texture.Get(), nullptr, &masks[index]));
    }
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    Check(d->CreateSamplerState(&sd, &sampler));
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    Check(d->CreateRasterizerState(&rd, &raster));
    c->RSSetState(raster.Get());
    D3D11_VIEWPORT vp{0, 0, 256, 256, 0, 1};
    c->RSSetViewports(1, &vp);
    auto* r = rtv.Get();
    c->OMSetRenderTargets(1, &r, nullptr);
    float black[4]{};
    c->ClearRenderTargetView(r, black);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs.Get(), nullptr, 0);
    c->PSSetShader(ps.Get(), nullptr, 0);
    auto* l = lighting.Get();
    c->PSSetConstantBuffers(7, 1, &l);
    auto* v = view.Get();
    c->PSSetConstantBuffers(2, 1, &v);
    auto* m = metadata.Get();
    c->VSSetConstantBuffers(0, 1, &m);
    auto* s = srv.Get();
    c->PSSetShaderResources(1, 1, &s);
    auto* sm = sampler.Get();
    c->PSSetSamplers(3, 1, &sm);
    c->PSSetSamplers(5, 1, &sm);
    c->PSSetSamplers(8, 1, &sm);
    for (unsigned i = 0; i < 2; ++i) {
        auto* mask = masks[i].Get();
        c->PSSetShaderResources(i ? 95 : 85, 1, &mask);
    }
    c->Draw(6, 0);
    c->CopyResource(read.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    Check(c->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    unsigned bad = 0, total = 0;
    for (unsigned y = 4; y < 252; ++y) {
        auto* row = (unsigned char*)mapped.pData + y * mapped.RowPitch;
        for (unsigned x = 4; x < 252; ++x) {
            auto* p = row + x * 4;
            ++total;
            const float color[3]{26, 51, 230}, fill[3]{.38f, .41f, .44f}, sun[3]{.1f, .15f, .2f};
            for (unsigned channel = 0; channel < 3; ++channel) {
                const float visibility =
                    mode == "night" || (mode == "shadow" && x < 128) ? 0.f : 1.f;
                const float ao = mode == "ao" && x < 128 ? .5f : 1.f;
                int expected =
                    int(std::lround(color[channel] *
                                    (live ? fill[channel] * ao + sun[channel] * visibility : 1.f)));
                if (baked) {
                    const float albedo[3]{128.f / 255, 160.f / 255, 192.f / 255};
                    const float irradiance[3]{64.f / 255, 80.f / 255, 102.f / 255};
                    const float linearAlbedo = std::pow((albedo[channel] + .055f) / 1.055f, 2.4f);
                    const float gain = mode == "baked_half" ? .5f : 1.f;
                    expected = int(
                        std::lround(255 * linearAlbedo *
                                    (2 * irradiance[channel] * gain + 128.f / 255 * sun[channel])));
                }
                if (abs(int(p[channel]) - expected) > 1) {
                    ++bad;
                    break;
                }
            }
        }
    }
    c->Unmap(read.Get(), 0);
    printf("WARP Replay map shader: %u/%u wrong pixels (%s)\n", bad, total, mode.c_str());
    return bad ? 1 : 0;
} catch (const std::exception& e) {
    puts(e.what());
    return 2;
}
