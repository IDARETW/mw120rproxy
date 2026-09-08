#pragma once
#include "iw8_zone.h"
#include <string>
#include <vector>
#include <array>
namespace replayrender {
struct Shader { unsigned type; std::string name,debugName; std::vector<uint8_t> header,program; };
struct Image { std::string name; uint16_t width,height; unsigned mipCount=1; std::vector<uint8_t> pixels; };
struct Technique { std::string name; std::vector<uint8_t> header,states,rootsig,statebits,args; std::array<std::string,4> shaders; };
struct Material {
    std::string material;
    std::vector<uint8_t> materialInfo,constants,bufferIndices,textureHeaders;
    std::string techset;
    std::vector<uint8_t> techsetHeader;
    std::vector<Shader> shaders;
    std::vector<Technique> techniques;
    std::vector<std::string> images;
    std::vector<Image> imageDefinitions;
    std::vector<std::array<std::vector<uint8_t>,4>> buffers;
};
struct Mesh : Material {
    std::vector<Material> additionalMaterials;
    std::vector<unsigned> surfaceMaterials;
    unsigned opaqueCount=0;
    std::vector<uint8_t> surfaces,bounds,drawSurfs,surfData,positions,aux,indices;
    unsigned count=0;
    unsigned words()const{return (count+31)/32;}
};
Mesh Load(const std::string& path);
void RegisterMaterial(iw8::ZoneWriter& writer,const std::string& meshPath);
void StampWorld(std::vector<uint8_t>& world,const Mesh& mesh);
void EmitSurfaces(iw8::ZoneWriter& writer,const Mesh& mesh);
void StampTransient(uint8_t* transient,const Mesh& mesh);
void EmitVertices(iw8::ZoneWriter& writer,const Mesh& mesh);
void EmitSortedSurfaces(iw8::ZoneWriter& writer,const Mesh& mesh);
}
