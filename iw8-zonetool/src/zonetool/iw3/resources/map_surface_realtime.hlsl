// Replay static-world inputs. Metadata and lightmap UVs use separate vertex attributes.
#define MW120R_ATLAS_VERTEX_V2 1
#ifndef MAP_DEBUG_MODE
#define MAP_DEBUG_MODE 0
#endif
#ifndef MAP_INDIRECT_GAIN
#define MAP_INDIRECT_GAIN 1.0
#endif
#ifndef MAP_UNBAKED_GAIN
#define MAP_UNBAKED_GAIN 1.0
#endif
#ifndef MAP_SOURCE_SUN_MASK
#define MAP_SOURCE_SUN_MASK 0
#endif
struct Input {
    float4 position : SV_POSITION;
    float4 uv : TEXCOORDS0;
    float2 lightmapUV : LMAPCOORDS0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    float4 color : COLOR0;
    nointerpolation float2 metadata : MAPDATA0;
    nointerpolation float4 materialParameters : MATERIALPARMS0;
    float3 relativePosition : WORLDPOS0;
};
Texture2D<float4> sourceAtlas : register(t1);
SamplerState colorSampler : register(s3);
Texture2D<float4> sunVisibility : register(t85);
StructuredBuffer<uint2> lightingTiles : register(t5);
SamplerState shadowSampler : register(s5);
cbuffer ReplayView : register(b2) {
    float4 replayView[8];
};
cbuffer ReplayLighting : register(b7) {
    float4 replayLighting[84];
};

#ifndef MAP_SOURCE_CHANNELS
#define MAP_SOURCE_CHANNELS 0
#endif
#if MAP_SOURCE_CHANNELS
Texture2D<float4> sourceNormalAtlas : register(t2);
Texture2D<float4> sourceResponseAtlas : register(t3);
float3 SourceData(float3 value) {
    return lerp(value * 12.92, 1.055 * pow(max(value, 0), 1.0 / 2.4) - .055, step(.0031308, value));
}
float3
SourceWorldNormal(Input input, uint flags, float2 atlasUV, float lod, out float3 tangentNormal) {
    tangentNormal = float3(0, 0, 1);
    if ((flags & 32) != 0) {
        float4 raw = sourceNormalAtlas.SampleLevel(colorSampler, atlasUV, lod);
        float2 slope = float2(raw.a, raw.g) * float2(4.08, 4.06451607) - float2(2.08, 2.06451607);
        tangentNormal = normalize(float3(slope, 1));
    }
    float3 normal = normalize(input.normal);
    float3 tangent = normalize(input.tangent.xyz);
    float3 bitangent = normalize(cross(normal, tangent)) * input.tangent.w;
    return normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent +
                     tangentNormal.z * normal);
}
float3 SourceIndirect(float2 uv, float3 first, float3 tangentNormal) {
    float4 responseData = sourceResponseAtlas.SampleLevel(colorSampler, uv, 0);
    float4 normalData = sourceNormalAtlas.SampleLevel(colorSampler, uv, 0);
    first = float3(normalData.ba, responseData.a);
    float3 second = responseData.rgb;
    float2 raw = normalData.rg;
    float2 slope = raw * float2(4.08, 4.06451607) - float2(2.08, 2.06451607);
    float response = saturate(dot(normalize(float3(slope, 1)), tangentNormal));
    return first * tangentNormal.z + second * response;
}
float3 SourceSunSpecular(Input input,
                         uint flags,
                         float2 uv,
                         float lod,
                         float3 normal,
                         float3 sunDirection,
                         float3 sun,
                         float visibility) {
    if ((flags & 64) == 0 || visibility <= 0)
        return 0;
    float4 raw = sourceResponseAtlas.SampleLevel(colorSampler, uv, lod);
    float3 view = normalize(-input.relativePosition);
    float3 reflected = reflect(-view, normal);
    float exponent = exp2(raw.a * 9.3775177) + 7;
    float lobe = saturate(exp((dot(reflected, sunDirection) - .99925) * exponent));
    float fresnel = pow(saturate(1 - abs(dot(view, normal))), input.materialParameters.z);
    float response = lerp(input.materialParameters.x, input.materialParameters.y, fresnel);
    return raw.rgb * response * input.materialParameters.w * lobe * sun * visibility;
}
#endif

float4 main(Input input) : SV_TARGET0 {
    uint tile = (uint)input.metadata.x;
    uint flags = (uint)input.metadata.y;
    uint kind = flags % 4;
    bool sky = kind == 1;
    float2 local = sky ? saturate(input.uv.xy) : frac(input.uv.xy);
    float2 cellSize = 4096.0 / ATLAS_COLUMNS;
    // Derivatives must use the continuous UV, before frac introduces tile seams.
    float footprint = max(length(ddx(input.uv.xy) * cellSize), length(ddy(input.uv.xy) * cellSize));
    float lod = sky ? 0 : clamp(log2(max(1.0, footprint)), 0, log2(cellSize.x) - 2);
    float border = .5 * exp2(ceil(lod));
    float2 pixel = float2(tile % ATLAS_COLUMNS, tile / ATLAS_COLUMNS) * cellSize +
                   clamp(.5 + local * (cellSize - 1), border, cellSize - border);
    float4 texel = sourceAtlas.SampleLevel(colorSampler, pixel / 4096.0, lod);
    texel *= input.color;
    uint alphaTest = (flags >> 3) & 3;
    if (kind == 3 && alphaTest == 1)
        texel.a = texel.a > 0 ? 1 : 0;
    if (kind == 3 && alphaTest == 2)
        texel.a = texel.a < .5 ? 1 : 0;
    // The generated depth prepass uses this same sampling code and cutoff.
    if (kind == 3)
        clip(texel.a - .5);
    float3 tangentNormal = float3(0, 0, 1);
    float3 shadingNormal = normalize(input.normal);
#if MAP_SOURCE_CHANNELS
    shadingNormal = SourceWorldNormal(input, flags, pixel / 4096.0, lod, tangentNormal);
#endif
    // The stock forward pass uses these same scaled screen coordinates, sun
    // direction, irradiance and sun visibility texture.
    float2 screenPixel = input.position.xy * replayView[7].x;
    float2 screenUV = screenPixel * replayView[6].zw;
    float3 direction = replayLighting[56].xyz;
    float3 sun = max(0, replayLighting[57].xyz);
    bool nativeSun = replayLighting[56].w != 0;
    float ndotl = nativeSun ? saturate(dot(shadingNormal, direction)) : 0;
    float visibility = 1;
    // Glass has no opaque depth coverage. Solid and masked surfaces both
    // contribute matching depth and normals before the shadow resolve.
    if (!sky && (kind == 0 || kind == 3)) {
        if (nativeSun) {
            // Replay's forward shader skips t85 for fully shadowed 8x8 tiles.
            // The resolve can leave those texels unwritten; sampling them
            // directly exposes stale data as camera-dependent blocks of light.
            uint2 tilePosition = (uint2)screenPixel >> 3;
            uint tileColumns = ((uint)replayLighting[54].x + 7) >> 3;
            uint tileClass = lightingTiles[tilePosition.y * tileColumns + tilePosition.x].y >> 28;
            if (replayLighting[83].w > 0 && tileClass == 11)
                visibility = 0;
            else
                visibility = saturate(abs(sunVisibility.SampleLevel(shadowSampler, screenUV, 0).x));
        }
    }
    // Soft hemispherical sky fill for props without a baked lightmap. Preserve
    // authored lightmap occlusion on architecture instead of a global exposure lift.
    float hemi = saturate(normalize(input.normal).z * .5 + .5);
    float3 indirect = lerp(float3(.22, .23, .24), float3(.38, .41, .44), hemi) * MAP_UNBAKED_GAIN;
    if ((flags & 4) != 0) {
        float4 lm = sourceAtlas.SampleLevel(colorSampler, saturate(input.lightmapUV), 0);
#if MAP_SOURCE_SUN_MASK
        // The source profile preserves the direction used to bake this mask.
        // Keep static occlusion at every distance; native shadows add moving
        // casters and nearby detail without erasing the authored building shade.
        visibility = min(visibility, lm.a);
#endif
        // CoD4 lightmap coefficients are linear bytes, but the shared color
        // atlas is an sRGB image. Undo its hardware transfer for this data tile
        // only; otherwise indirect lighting is incorrectly crushed toward black.
        float3 baked = lerp(lm.rgb * 12.92, 1.055 * pow(max(lm.rgb, 0), 1.0 / 2.4) - .055,
                            step(.0031308, lm.rgb));
#if MAP_SOURCE_CHANNELS
        indirect =
            SourceIndirect(saturate(input.lightmapUV), baked, tangentNormal) * MAP_INDIRECT_GAIN;
#else
        indirect = baked * 2 * MAP_INDIRECT_GAIN;
#endif
    }
    float3 light = indirect + visibility * ndotl * sun;
    if (sky)
        light = 1;
    // Build-time diagnostics keep depth coverage and native postprocessing.
#if MAP_DEBUG_MODE == 1
    light = 1;
#elif MAP_DEBUG_MODE == 2
    light = indirect;
#elif MAP_DEBUG_MODE == 3
    texel.rgb = visibility;
    light = 1;
#elif MAP_DEBUG_MODE == 4
    texel.rgb = shadingNormal * .5 + .5;
    light = 1;
#endif
    float3 specular = 0;
#if MAP_SOURCE_CHANNELS && MAP_DEBUG_MODE == 0
    if (!sky && nativeSun)
        specular = SourceSunSpecular(input, flags, pixel / 4096.0, lod, shadingNormal, direction,
                                     sun, visibility);
#endif
    return float4(min((texel.rgb * light + specular) * replayLighting[42].x, 32255.0),
                  kind == 2 ? texel.a : 1);
}
