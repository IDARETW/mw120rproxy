// Replay 1.20 static BSP layout 35: 88-byte surface records and raw vertex streams.
struct SurfaceData {
    uint transientZone;
    uint layerCount;
    uint positionOffset;
    uint tangentOffset;
    uint lightmapOffset;
    uint colorOffset;
    uint textureOffset;
    uint unused[15];
};
StructuredBuffer<SurfaceData> surfaces : register(t13);
ByteAddressBuffer positions : register(t14);
ByteAddressBuffer attributes : register(t15);
cbuffer ReplayView : register(b2) {
    float4 replayView[6];
};
cbuffer ReplaySurface : register(b9) {
    uint4 surfaceIndex;
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
Output main(uint id : SV_VertexID) {
    SurfaceData s = surfaces[surfaceIndex.x];
    precise float3 p = asfloat(positions.Load3(s.positionOffset + id * 12));
    precise float3 offset = (float3)(-asint(replayView[5].xyz)) / 4096.0;
    precise float4 relative;
    relative.x = dot(float4(p, offset.x), float4(1, 0, 0, 1));
    relative.y = dot(float4(p, offset.y), float4(0, 1, 0, 1));
    relative.z = dot(float4(p, offset.z), float4(0, 0, 1, 1));
    relative.w = 1;
    Output o;
    o.relativePosition = relative.xyz;
    o.position.x = dot(relative, replayView[0]);
    o.position.y = dot(relative, replayView[1]);
    o.position.z = dot(relative, replayView[2]);
    o.position.w = dot(relative, replayView[3]);
    o.uv = float4(asfloat(attributes.Load2(s.textureOffset + id * s.layerCount * 8)), 0, 0);
    if (s.layerCount >= 2)
        o.uv.zw = asfloat(attributes.Load2(s.textureOffset + id * s.layerCount * 8 + 8));
    o.metadata = o.uv.zw;
    o.materialParameters =
        s.layerCount >= 4 ? asfloat(attributes.Load4(s.textureOffset + id * s.layerCount * 8 + 16))
                          : float4(.8, 4, 2.5, .625);
    o.lightmapUV = s.lightmapOffset ? asfloat(attributes.Load2(s.lightmapOffset + id * 8)) : 0;
    uint packed = attributes.Load(s.tangentOffset + id * 4);
    float3 xyz = (float3(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 511) *
                      float3(2.0 / 1023, 2.0 / 1023, 2.0 / 511) -
                  1) *
                 .7071067811865475;
    float largest = sqrt(1 - min(dot(xyz, xyz), 1));
    uint largestIndex = packed >> 30;
    float4 q = largestIndex == 0   ? float4(largest, xyz)
               : largestIndex == 1 ? float4(xyz.x, largest, xyz.yz)
               : largestIndex == 2 ? float4(xyz.xy, largest, xyz.z)
                                   : float4(xyz, largest);
    o.normal = float3(2 * (q.x * q.z + q.w * q.y), 2 * (q.y * q.z - q.w * q.x),
                      1 - 2 * (q.x * q.x + q.y * q.y));
    o.tangent = float4(1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y + q.w * q.z),
                       2 * (q.x * q.z - q.w * q.y), (packed & (1u << 29)) ? -1 : 1);
    uint color = s.colorOffset ? attributes.Load(s.colorOffset + id * 4) : 0xffffffff;
    o.color = float4(color & 255, (color >> 8) & 255, (color >> 16) & 255, color >> 24) / 255.0;
    return o;
}
