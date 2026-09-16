// Replay 1.20 static-model UGB layout. SV_VertexID carries the draw-surface
// index in its upper 16 bits and the surface-local vertex in its lower bits.
ByteAddressBuffer drawSurfaces : register(t63);
ByteAddressBuffer unifiedGeometry : register(t64);
ByteAddressBuffer vertexPages : register(t65);
ByteAddressBuffer vertexColors : register(t66);
ByteAddressBuffer colorPages : register(t67);

struct SurfaceBounds {
    float4 midpointAndScale;
    float4 unused;
};
StructuredBuffer<SurfaceBounds> surfaceBounds : register(t75);

struct ModelPlacement {
    int3 origin;
    uint quaternionXY;
    uint quaternionZW;
    float scale;
};
StructuredBuffer<ModelPlacement> modelPlacements : register(t76);

cbuffer ReplayView : register(b2) {
    float4 replayView[6];
};

struct Output {
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

float4 DecodePlacementQuaternion(uint xy, uint zw) {
    float4 quaternion =
        float4(xy & 0xffff, xy >> 16, zw & 0xffff, zw >> 16) * (2.0 / 65535.0) - 1.0;
    return quaternion;
}

float3 Rotate(float3 value, float4 quaternion) {
    return value + 2.0 * cross(quaternion.xyz,
                               cross(quaternion.xyz, value) + quaternion.w * value);
}

void DecodeTangentFrame(uint packed, out float3 normal, out float3 tangent,
                        out float binormalSign) {
    float3 xyz =
        (float3(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 511) *
             float3(2.0 / 1023, 2.0 / 1023, 2.0 / 511) -
         1) *
        .7071067811865475;
    float largest = sqrt(1 - min(dot(xyz, xyz), 1));
    uint largestIndex = packed >> 30;
    float4 q = largestIndex == 0   ? float4(largest, xyz)
               : largestIndex == 1 ? float4(xyz.x, largest, xyz.yz)
               : largestIndex == 2 ? float4(xyz.xy, largest, xyz.z)
                                   : float4(xyz, largest);
    normal = float3(2 * (q.x * q.z + q.w * q.y), 2 * (q.y * q.z - q.w * q.x),
                    1 - 2 * (q.x * q.x + q.y * q.y));
    tangent = float3(1 - 2 * (q.y * q.y + q.z * q.z),
                     2 * (q.x * q.y + q.w * q.z),
                     2 * (q.x * q.z - q.w * q.y));
    binormalSign = (packed & (1u << 29)) ? -1 : 1;
}

Output main(uint id : SV_VertexID) {
    uint drawSurface = id >> 16;
    uint3 surface = drawSurfaces.Load3(drawSurface * 16);
    uint page = vertexPages.Load((surface.z + ((id >> 6) & 1023)) * 4);

    uint attributeVertex = (id & 63) | (page << 6);
    uint3 attributes = unifiedGeometry.Load3(0x02bc0000 + attributeVertex * 12);
    uint positionAddress = ((id * 8) & 511) | (page << 9);
    uint2 packedPosition = unifiedGeometry.Load2(positionAddress);

    uint3 quantized;
    quantized.x = packedPosition.x & 0x1fffff;
    quantized.y = (packedPosition.x >> 21) | ((packedPosition.y & 0x3ff) << 11);
    quantized.z = (packedPosition.y >> 10) & 0x1fffff;
    float4 bounds = surfaceBounds[surface.y & 0xffff].midpointAndScale;
    precise float3 local = bounds.xyz +
                           ((float3)quantized * (2.0 / 2097151.0) - 1.0) * bounds.w;

    ModelPlacement placement = modelPlacements[surface.x];
    float4 placementQuaternion =
        DecodePlacementQuaternion(placement.quaternionXY, placement.quaternionZW);
    precise float3 translation =
        (float3)(placement.origin - asint(replayView[5].xyz)) / 4096.0;
    precise float4 relative =
        float4(Rotate(local * placement.scale, placementQuaternion) + translation, 1);

    Output o;
    o.relativePosition = relative.xyz;
    o.position.x = dot(relative, replayView[0]);
    o.position.y = dot(relative, replayView[1]);
    o.position.z = dot(relative, replayView[2]);
    o.position.w = dot(relative, replayView[3]);
    o.uv = float4(f16tof32(attributes.z & 0xffff), f16tof32(attributes.z >> 16), 0, 0);
    o.lightmapUV = 0;

    float3 modelNormal;
    float3 modelTangent;
    float binormalSign;
    DecodeTangentFrame(attributes.y, modelNormal, modelTangent, binormalSign);
    o.normal = Rotate(modelNormal, placementQuaternion);
    o.tangent = float4(Rotate(modelTangent, placementQuaternion), binormalSign);

    o.metadata = float2(attributes.x & 0xffff, (attributes.x >> 16) & 0xff);
    o.materialParameters = float4(.8, 4, 2.5, .625);
    uint colorPageStart = drawSurfaces.Load(0x00240000 + drawSurface * 4);
    uint colorByte = (id & 0xffff) * 4;
    uint colorPage = colorPages.Load((colorPageStart + (colorByte >> 10)) * 4);
    uint packedColor = vertexColors.Load((colorPage << 10) | (colorByte & 1023));
    float4 color = float4(packedColor & 255, (packedColor >> 8) & 255,
                         (packedColor >> 16) & 255, packedColor >> 24) / 255.0;
    // Replay decodes authored model RGB from sRGB; alpha remains linear.
    o.color = float4(lerp(color.rgb / 12.92, pow((color.rgb + .055) / 1.055, 2.4),
                          step(.04045, color.rgb)), color.a);
    return o;
}
