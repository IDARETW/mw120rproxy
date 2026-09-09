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
    const bool tileShadows = mode.rfind("shadow_tiles", 0) == 0;
    const bool tileShadowsEnabled = tileShadows && mode != "shadow_tiles_disabled";
    const float tileScale = mode == "shadow_tiles_scaled" ? .65f : 1.f;
    const unsigned tileWidth = unsigned(std::ceil(256 * tileScale));
    const unsigned tileColumns = (tileWidth + 7) / 8;
    auto tileClass = [](unsigned x, unsigned y) {
        return ((x * 7 + y * 13) % 5 == 0) ? 11u : 3u;
    };
    const bool sourceChannels = mode.rfind("source_", 0) == 0;
    const bool sourceMask = mode.rfind("source_mask_", 0) == 0;
    const bool applySourceMask = sourceMask && mode != "source_mask_other_sun";
    const bool sourceBaked = mode == "source_baked" || mode == "source_baked_flat" || sourceMask;
    const bool sourceSpecular = mode.rfind("source_specular", 0) == 0;
    const bool sourceNormalDepth = mode == "source_normal_depth";
    const bool depthCoverage =
        mode == "depth_coverage" || mode == "depth_coverage_shifted" || sourceNormalDepth;
    const bool shadowCoverage = mode == "shadow_coverage" || mode == "shadow_coverage_shifted";
    const bool coverage = (depthCoverage && !sourceNormalDepth) || shadowCoverage;
    const double coverageShift =
        mode == "depth_coverage_shifted" || mode == "shadow_coverage_shifted" ? .3125 : 0;
    const bool aoPattern = mode == "ao_pattern" || mode == "ao_pattern_moved";
    const bool baked = mode == "baked" || mode == "baked_half" || aoPattern;
    const bool vertexColor =
        mode == "vertex_opaque" || mode == "vertex_glass" || mode == "vertex_mask";
    const bool live = mode == "shadow" || mode == "ao" || mode == "night" || mode == "glass" ||
                      baked || vertexColor || sourceChannels || tileShadows;
    std::ifstream file(argv[1]);
    std::stringstream input;
    input << file.rdbuf();
    std::string source = input.str();
    const bool realtimeShader = source.find("bool nativeSun") != std::string::npos;
    if (sourceChannels)
        source = "#define MAP_SOURCE_CHANNELS 1\n" + source;
    if (applySourceMask)
        source = "#define MAP_SOURCE_SUN_MASK 1\n" + source;
    if (mode == "baked_half" || mode == "sky_half")
        source = "#define MAP_INDIRECT_GAIN 0.5\n#define MAP_UNBAKED_GAIN 0.5\n" + source;
    Replace(source, "ATLAS_COLUMNS", "4");
    Replace(source, "SUN_DIRECTION", "float3(0,0,1)");
    Replace(source, "SUN_COLOR", "float3(1,1,1)");
    auto psCode = Compile(source, "ps_5_0");
    std::string vertexSource = R"(
cbuffer Fixture : register(b0) {
    float2 metadata;
};
struct Output {
    float4 position : SV_POSITION;
    float4 uv : TEXCOORDS0;
    float2 lightmapUV : LMAPCOORDS0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float4 color : COLOR0;
    nointerpolation float2 mapData : MAPDATA0;
    nointerpolation float4 materialParameters : MATERIALPARMS0;
    float3 relativePosition : WORLDPOS0;
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
    o.color = 1;
    o.mapData = floor(metadata);
    o.materialParameters = float4(.4, 3, 2, .625);
    o.relativePosition = float3(0, 0, -4);
    return o;
}
 )";
    if (source.find("MW120R_ATLAS_VERTEX_V2") != std::string::npos) {
        Replace(vertexSource, "o.lightmapUV = metadata;",
                "o.uv.zw = floor(metadata); o.lightmapUV = frac(metadata) * 4 - 1;");
    }
    if (vertexColor)
        Replace(vertexSource, "o.color = 1;", "o.color = float4(.5, .25, .75, .25);");
    if (coverageShift)
        Replace(vertexSource, "p[i] * .5 + .5", "p[i] * .5 + .8125");
    auto vsCode = Compile(vertexSource, "vs_5_0");
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
            if (baked || sourceBaked)
                pixels[y * 4096 + x] = tile == 9 ? 0xFFC0A080 : 0x80665040;
            if (mode == "source_mask_blocked" && tile != 9)
                pixels[y * 4096 + x] &= 0xFFFFFF;
            if (coverage)
                pixels[y * 4096 + x] = (x % 1024 >= 512 ? 0xFF000000u : 0u) | 0xFFFFFFu;
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
    if (baked || sourceChannels)
        tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    ComPtr<ID3D11Texture2D> atlas;
    Check(d->CreateTexture2D(&tex, &data, &atlas));
    ComPtr<ID3D11ShaderResourceView> srv;
    Check(d->CreateShaderResourceView(atlas.Get(), nullptr, &srv));
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> sourceMaps;
    if (sourceChannels) {
        tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        for (unsigned index = 0; index < 2; ++index) {
            for (unsigned y = 0; y < 4096; ++y)
                for (unsigned x = 0; x < 4096; ++x) {
                    const unsigned tile = x / 1024 + 4 * (y / 1024);
                    // At the material tile A/G encode slopes (2,2), whose unit
                    // normal is (2/3,2/3,1/3). The lightmap direction matches it.
                    pixels[y * 4096 + x] = index == 0 ? (tile == 9 ? 0xFF00FF00u : 0x5040FFFFu)
                                                      : (tile == 9 ? 0x80604020u : 0x66201810u);
                }
            ComPtr<ID3D11Texture2D> texture;
            Check(d->CreateTexture2D(&tex, &data, &texture));
            Check(d->CreateShaderResourceView(texture.Get(), nullptr, &sourceMaps[index]));
        }
    }
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
    if (depthCoverage)
        desc.Format = DXGI_FORMAT_R32_FLOAT;
    if (shadowCoverage) {
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    }
    ComPtr<ID3D11Texture2D> target, read;
    Check(d->CreateTexture2D(&desc, nullptr, &target));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(d->CreateTexture2D(&desc, nullptr, &read));
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11DepthStencilState> depthState;
    if (shadowCoverage) {
        D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
        viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
        viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        Check(d->CreateDepthStencilView(target.Get(), &viewDesc, &dsv));
        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        Check(d->CreateDepthStencilState(&depthDesc, &depthState));
    } else {
        Check(d->CreateRenderTargetView(target.Get(), nullptr, &rtv));
    }
    std::vector<float> constants(84 * 4);
    constants[42 * 4] = 1;
    constants[56 * 4 + 2] = 1;
    constants[56 * 4 + 3] = mode == "night" || mode == "source_specular_night" ? 0.f : 1.f;
    constants[57 * 4] = .1f;
    constants[57 * 4 + 1] = .15f;
    constants[57 * 4 + 2] = .2f;
    constants[47 * 4] = mode == "ao" || aoPattern ? 1.f : 0.f;
    constants[47 * 4 + 1] = 1;
    constants[54 * 4] = float(tileWidth);
    constants[83 * 4 + 3] = tileShadowsEnabled ? 1.f : 0.f;
    D3D11_BUFFER_DESC cb{
        unsigned(constants.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA cbd{constants.data(), 0, 0};
    ComPtr<ID3D11Buffer> lighting;
    Check(d->CreateBuffer(&cb, &cbd, &lighting));
    const bool biased = mode == "biased" || mode == "sky_half" || live;
    float meta[]{9 + (biased ? .25f : 0),
                 (live ? (mode == "glass" ? 2.f : 0.f) : 1.f) + (biased ? .25f : 0), 0, 0};
    if (coverage) {
        meta[0] = 9.25f;
        meta[1] = 3.25f;
    }
    if (mode == "shadow_tiles_masked")
        meta[1] = 3.25f;
    if (baked) {
        meta[0] = 9.25f + .25f * .125f;
        meta[1] = 4.25f + .25f * .125f;
    }
    if (vertexColor)
        meta[1] = (mode == "vertex_glass" ? 2.f : mode == "vertex_mask" ? 3.f : 0.f) + .25f;
    if (sourceChannels) {
        unsigned flags = sourceSpecular ? 64 : 32;
        if (sourceBaked) {
            flags = 4 | (mode == "source_baked_flat" ? 0 : 32);
            meta[0] = 9.25f + .25f * .125f;
            meta[1] = float(flags) + .25f + .25f * .125f;
        } else {
            meta[0] = 9.25f;
            meta[1] = float(flags) + .25f;
        }
    }
    cb.ByteWidth = 16;
    cbd.pSysMem = meta;
    ComPtr<ID3D11Buffer> metadata;
    Check(d->CreateBuffer(&cb, &cbd, &metadata));
    std::array<float, 32> viewConstants{};
    viewConstants[6 * 4 + 2] = viewConstants[6 * 4 + 3] = 1.f / 256;
    viewConstants[7 * 4] = tileScale;
    cb.ByteWidth = sizeof(viewConstants);
    cbd.pSysMem = viewConstants.data();
    ComPtr<ID3D11Buffer> view;
    Check(d->CreateBuffer(&cb, &cbd, &view));
    std::vector<std::array<unsigned, 2>> tiles(tileColumns * tileColumns);
    for (unsigned y = 0; y < tileColumns; ++y)
        for (unsigned x = 0; x < tileColumns; ++x)
            tiles[y * tileColumns + x] = {0xB0000000u, (tileClass(x, y) << 28) | 0x01234567u};
    D3D11_BUFFER_DESC tileDesc{unsigned(tiles.size() * 8), D3D11_USAGE_IMMUTABLE,
                             D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 8};
    D3D11_SUBRESOURCE_DATA tileData{tiles.data(), 0, 0};
    ComPtr<ID3D11Buffer> tileBuffer;
    Check(d->CreateBuffer(&tileDesc, &tileData, &tileBuffer));
    ComPtr<ID3D11ShaderResourceView> tileView;
    Check(d->CreateShaderResourceView(tileBuffer.Get(), nullptr, &tileView));
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> masks;
    for (unsigned index = 0; index < 2; ++index) {
        std::vector<std::array<float, 4>> mask(256 * 256);
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned x = 0; x < 256; ++x) {
                float value = 1;
                if (!index &&
                    (mode == "shadow" || mode == "glass" || mode == "source_specular_shadow" ||
                     mode == "source_mask_near"))
                    value = x < 128 ? 0.f : 1.f;
                if (index && mode == "ao")
                    value = x < 128 ? .5f : 1.f;
                if (index && aoPattern) {
                    const unsigned shift = mode == "ao_pattern_moved" ? 47 : 0;
                    value = ((x + shift) % 61 < y % 43) ? 0.f : 1.f;
                }
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
    if (shadowCoverage) {
        c->OMSetRenderTargets(0, nullptr, dsv.Get());
        c->OMSetDepthStencilState(depthState.Get(), 0);
        c->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 0, 0);
    } else if (depthCoverage) {
        ID3D11RenderTargetView* targets[]{nullptr, r};
        c->OMSetRenderTargets(2, targets, nullptr);
    } else {
        c->OMSetRenderTargets(1, &r, nullptr);
    }
    float black[4]{};
    if (r)
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
    auto* tilesSrv = tileView.Get();
    c->PSSetShaderResources(5, 1, &tilesSrv);
    if (sourceChannels)
        for (unsigned index = 0; index < 2; ++index) {
            auto* map = sourceMaps[index].Get();
            c->PSSetShaderResources(index + 2, 1, &map);
        }
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
            if (sourceNormalDepth) {
                // Material normal slopes must not enter the geometric prepass.
                const unsigned expected = (511u << 0) | (511u << 10) | (1023u << 20);
                unsigned actual;
                memcpy(&actual, p, 4);
                if (actual != expected)
                    ++bad;
                continue;
            }
            if (coverage) {
                const double u = (x + .5) / 256, v = 1 - (y + .5) / 256;
                double uv;
                if (u + v <= 1) {
                    uv = (u / 7.13) / ((1 - u - v) / .17 + u / 7.13 + v / 2.7);
                } else {
                    uv = ((1 - v) / 7.13 + (u + v - 1) / 1.4) /
                         ((1 - u) / 2.7 + (1 - v) / 7.13 + (u + v - 1) / 1.4);
                }
                uv += coverageShift;
                uv -= std::floor(uv);
                const unsigned expected =
                    uv >= .5 ? (shadowCoverage ? 0x3F000000u : (1023u << 20) | (511u << 10) | 511u)
                             : 0u;
                unsigned actual;
                memcpy(&actual, p, 4);
                if (actual != expected)
                    ++bad;
                continue;
            }
            if (mode == "vertex_mask") {
                if (p[0] || p[1] || p[2] || p[3])
                    ++bad;
                continue;
            }
            const float color[3]{26, 51, 230}, fill[3]{.38f, .41f, .44f}, sun[3]{.1f, .15f, .2f};
            for (unsigned channel = 0; channel < 3; ++channel) {
                float visibility =
                    mode == "night" || (mode == "shadow" && x < 128) ? 0.f : 1.f;
                if (tileShadowsEnabled &&
                    tileClass(unsigned((x + .5f) * tileScale) / 8,
                              unsigned((y + .5f) * tileScale) / 8) == 11)
                    visibility = 0;
                int expected = int(std::lround(
                    color[channel] * (live ? fill[channel] + sun[channel] * visibility : 1.f)));
                if (baked) {
                    const float albedo[3]{128.f / 255, 160.f / 255, 192.f / 255};
                    const float irradiance[3]{64.f / 255, 80.f / 255, 102.f / 255};
                    const float linearAlbedo = std::pow((albedo[channel] + .055f) / 1.055f, 2.4f);
                    const float gain = mode == "baked_half" ? .5f : 1.f;
                    expected = int(
                        std::lround(255 * linearAlbedo *
                                    (2 * irradiance[channel] * gain +
                                     (realtimeShader ? 1.f : 128.f / 255) * sun[channel])));
                }
                if (vertexColor) {
                    const float tint[]{.5f, .25f, .75f};
                    expected = int(std::lround(color[channel] * (fill[channel] + sun[channel]) *
                                               tint[channel]));
                }
                if (sourceChannels) {
                    const float sunValue = realtimeShader ? sun[channel] : .65f;
                    const float visibility = mode == "source_specular_night" ||
                                                     ((mode == "source_specular_shadow" ||
                                                       mode == "source_mask_near") && x < 128)
                                                 ? 0.f
                                                 : 1.f;
                    const float albedoBytes[]{128, 160, 192};
                    const float srgb = (sourceBaked ? albedoBytes[channel] : color[channel]) / 255;
                    const float albedo =
                        srgb <= .04045f ? srgb / 12.92f : std::pow((srgb + .055f) / 1.055f, 2.4f);
                    const float nz = sourceSpecular || mode == "source_baked_flat" ? 1.f : 1.f / 3;
                    float indirect = fill[channel];
                    float shadow = visibility;
                    if (sourceBaked) {
                        const float first[]{64.f / 255, 80.f / 255, 102.f / 255};
                        const float second[]{16.f / 255, 24.f / 255, 32.f / 255};
                        indirect = first[channel] * nz +
                                   second[channel] * (mode == "source_baked_flat" ? 1.f / 3 : 1.f);
                        if (!realtimeShader)
                            shadow *= 128.f / 255;
                        if (applySourceMask)
                            shadow = (std::min)(shadow, mode == "source_mask_blocked" ? 0.f : 128.f / 255);
                    }
                    const float response[]{32.f / 255, 64.f / 255, 96.f / 255};
                    const float specular =
                        sourceSpecular ? response[channel] * .4f * .625f * sunValue * visibility
                                       : 0;
                    expected = int(std::lround(
                        255 * (albedo * (indirect + nz * sunValue * shadow) + specular)));
                }
                if (abs(int(p[channel]) - expected) > 1) {
                    ++bad;
                    break;
                }
            }
            if (vertexColor && abs(int(p[3]) - (mode == "vertex_glass" ? 64 : 255)) > 1)
                ++bad;
        }
    }
    c->Unmap(read.Get(), 0);
    printf("WARP Replay map shader: %u/%u wrong pixels (%s)\n", bad, total, mode.c_str());
    return bad ? 1 : 0;
} catch (const std::exception& e) {
    puts(e.what());
    return 2;
}
