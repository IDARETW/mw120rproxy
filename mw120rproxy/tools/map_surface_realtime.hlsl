// Replay static-world inputs. UV fractions retain the authored lightmap coordinates.
#ifndef MAP_INDIRECT_GAIN
#define MAP_INDIRECT_GAIN 1.0
#endif
#ifndef MAP_UNBAKED_GAIN
#define MAP_UNBAKED_GAIN 1.0
#endif
struct Input {
    float4 position : SV_POSITION;
    float4 uv : TEXCOORDS0;
    float2 lightmapUV : LMAPCOORDS0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
};
Texture2D<float4> sourceAtlas : register(t1);
SamplerState colorSampler : register(s3);
Texture2D<float4> sunVisibility : register(t85);
Texture2D<float4> sceneOcclusion : register(t95);
SamplerState shadowSampler : register(s5);
SamplerState screenSampler : register(s8);
cbuffer ReplayView : register(b2) {
    float4 replayView[8];
};
cbuffer ReplayLighting : register(b7) {
    float4 replayLighting[58];
};
float4 main(Input input) : SV_TARGET0 {
    uint tile = (uint)floor(input.lightmapUV.x);
    uint flags = (uint)floor(input.lightmapUV.y);
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
    // Cutouts have their own depth-writing lit pass; no opaque card prepass.
    if (kind == 3)
        clip(texel.a - .5);
    // The stock forward pass uses these same scaled screen coordinates, sun
    // direction, irradiance, visibility texture and indirect-occlusion controls.
    float2 screenUV = input.position.xy * replayView[7].x * replayView[6].zw;
    float3 direction = replayLighting[56].xyz;
    float3 sun = max(0, replayLighting[57].xyz);
    bool nativeSun = replayLighting[56].w != 0;
    float ndotl = nativeSun ? saturate(dot(normalize(input.normal), direction)) : 0;
    float visibility = 1;
    float occlusion = 1;
    // Screen-space buffers describe the opaque depth prepass. Glass and
    // cutouts currently have no matching prepass, so never project another
    // surface's contact shadow onto them.
    if (!sky && kind == 0) {
        if (nativeSun)
            visibility = saturate(abs(sunVisibility.SampleLevel(shadowSampler, screenUV, 0).x));
        float ao = sceneOcclusion.SampleLevel(screenSampler, screenUV, 0).x;
        occlusion = saturate(1 + replayLighting[47].x * (saturate(ao * replayLighting[47].y) - 1));
    }
    // Soft hemispherical sky fill for props without a baked lightmap. Preserve
    // authored lightmap occlusion on architecture instead of a global exposure lift.
    float hemi = saturate(normalize(input.normal).z * .5 + .5);
    float3 indirect = lerp(float3(.22, .23, .24), float3(.38, .41, .44), hemi) * MAP_UNBAKED_GAIN;
    float bakedVisibility = 1;
    if (flags >= 4) {
        float4 lm =
            sourceAtlas.SampleLevel(colorSampler, saturate(frac(input.lightmapUV) * 4 - 1), 0);
        // CoD4 lightmap coefficients are linear bytes, but the shared color
        // atlas is an sRGB image. Undo its hardware transfer for this data tile
        // only; otherwise indirect lighting is incorrectly crushed toward black.
        float3 baked = lerp(lm.rgb * 12.92, 1.055 * pow(max(lm.rgb, 0), 1.0 / 2.4) - .055,
                            step(.0031308, lm.rgb));
        indirect = baked * 2 * MAP_INDIRECT_GAIN;
        bakedVisibility = lm.a;
    }
    // Minimum preserves authored static occlusion without squaring the same
    // shadow where the live and baked masks agree. AO affects indirect only.
    float3 light = indirect * occlusion + min(bakedVisibility, visibility) * ndotl * sun;
    if (sky)
        light = 1;
    return float4(min(texel.rgb * light * replayLighting[42].x, 32255.0), kind == 2 ? texel.a : 1);
}
