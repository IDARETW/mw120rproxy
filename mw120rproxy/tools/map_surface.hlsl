// Replay static-world inputs. UV fractions retain the authored lightmap coordinates.
struct Input {
    float4 position : SV_POSITION;
    float4 uv : TEXCOORDS0;
    float2 lightmapUV : LMAPCOORDS0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
};
Texture2D<float4> sourceAtlas : register(t1);
SamplerState colorSampler : register(s3);
cbuffer ReplayLighting : register(b7) {
    float4 replayLighting[43];
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
    float ndotl = saturate(dot(normalize(input.normal), SUN_DIRECTION));
    // Soft hemispherical sky fill for props without a baked lightmap. Preserve
    // authored lightmap occlusion on architecture instead of a global exposure lift.
    float hemi = saturate(normalize(input.normal).z * .5 + .5);
    float3 light =
        lerp(float3(.22, .23, .24), float3(.38, .41, .44), hemi) + ndotl * SUN_COLOR * .65;
    if (flags >= 4) {
        float4 lm =
            sourceAtlas.SampleLevel(colorSampler, saturate(frac(input.lightmapUV) * 4 - 1), 0);
        // CoD4 lightmap coefficients are linear bytes, but the shared color
        // atlas is an sRGB image. Undo its hardware transfer for this data tile
        // only; otherwise indirect lighting is incorrectly crushed toward black.
        float3 baked = lerp(
            lm.rgb * 12.92, 1.055 * pow(max(lm.rgb, 0), 1.0 / 2.4) - .055, step(.0031308, lm.rgb));
        light = baked * 2 + lm.a * ndotl * SUN_COLOR * .65;
    }
    if (sky)
        light = 1;
    return float4(min(texel.rgb * light * replayLighting[42].x, 32255.0), kind == 2 ? texel.a : 1);
}
