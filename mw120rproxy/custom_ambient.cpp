#include "custom_ambient.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
uintptr_t g_base=0;
using Update=void(*)(void*,const void*,const void*);
std::atomic<Update> g_update{nullptr};
std::string g_world;
uintptr_t g_resource=0;
bool g_custom=false;
bool Probe(const std::string& id,std::array<unsigned char,64>& probe){
    for(const auto& p:custommaps::List())if(p.valid && p.id==id){
        std::ifstream f(std::filesystem::path(p.directory)/"ambient.bin",std::ios::binary);
        char magic[8]{};
        return f.read(magic,8) && !memcmp(magic,"MWRAMB01",8) &&
            f.read(reinterpret_cast<char*>(probe.data()),64) && f.peek()==EOF;
    }
    return false;
}
void UpdateProbe(void* state,const void* data,const void* view){
    uintptr_t world=0,name=0;char text[96]{};const auto id=custommaps::Active();
    const bool custom=!id.empty() && safemem::ReadBytes(reinterpret_cast<void*>(g_base+0x10C77870),&world,8) && world &&
        safemem::ReadBytes(reinterpret_cast<void*>(world),&name,8) &&
        safemem::ReadString(reinterpret_cast<const char*>(name),text,sizeof(text)) && text=="maps/mp/"+id+".d3dbsp";
    if(!custom){
        // Restore the normal game's probe on its next native update, including
        // returning to the same zone that preceded the custom map.
        if(g_custom){*reinterpret_cast<unsigned char*>(g_base+0xFB942B8)=1;g_custom=false;g_world.clear();g_resource=0;}
        g_update.load()(state,data,view);return;
    }
    unsigned index=~0u,count=0;uintptr_t shader=0,resource=0;
    if(!safemem::ReadBytes(static_cast<const char*>(data)+0x113164,&index,4) || index>1 ||
       !safemem::ReadBytes(reinterpret_cast<void*>(g_base+0xFB94360+index*0xF8),&count,4) || count){g_update.load()(state,data,view);return;}
    if(!safemem::ReadBytes(reinterpret_cast<void*>(g_base+0x10C78D40),&shader,8) || !shader ||
       !safemem::ReadBytes(reinterpret_cast<void*>(g_base+0xFB94258),&resource,8) || !resource)return;
    if(g_custom && g_world==id && g_resource==resource)return;
    std::array<unsigned char,64> probe{};if(!Probe(id,probe))return;
    auto* compute=reinterpret_cast<void*(*)(void*)>(g_base+replay::GetComputeState.rva)(state);
    uintptr_t device=0;if(!compute || !safemem::ReadBytes(static_cast<char*>(compute)+0xC50,&device,8))return;
    reinterpret_cast<void(*)(uintptr_t)>(g_base+replay::LockGfxImmediate.rva)(device);
    reinterpret_cast<void(*)(void*,void*)>(g_base+replay::SetComputeShader.rva)(compute,reinterpret_cast<void*>(shader));
    // The exact Replay compute shader's CB[1].w == 0 branch copies CB[2..5]
    // directly to the 64-byte fallback UAV and performs NO light-grid SRV reads.
    // Keep the engine's packing and resource lifecycle; no fabricated light grid.
    alignas(16) std::array<unsigned char,96> constants{};memcpy(constants.data()+32,probe.data(),64);
    reinterpret_cast<void(*)(void*,unsigned,const void*,unsigned,void*)>(g_base+replay::UploadComputeConstants.rva)(compute,0,constants.data(),96,nullptr);
    const void* rw=reinterpret_cast<void*>(g_base+0xFB94298);
    reinterpret_cast<void(*)(void*,unsigned,unsigned,const void* const*,const unsigned*)>(g_base+replay::SetComputeRWViews.rva)(compute,0,1,&rw,nullptr);
    reinterpret_cast<void(*)(void*,unsigned,unsigned,unsigned)>(g_base+replay::ComputeDispatch.rva)(compute,1,1,1);
    reinterpret_cast<void(*)(uintptr_t)>(g_base+replay::UnlockGfxImmediate.rva)(device);
    g_custom=true;g_world=id;g_resource=resource;
    LOG_INFO("Ambient","uploaded sky-derived fallback SH probe map=%s (native arms, weapons and dynamic models)",id.c_str());
}
}
namespace customambient {
hook::Status Install(uintptr_t base){
    if(g_update.load())return hook::Status::Installed;g_base=base;
    for(const auto* b:{&replay::GetComputeState,&replay::SetComputeShader,&replay::UploadComputeConstants,&replay::SetComputeRWViews,&replay::ComputeDispatch,&replay::LockGfxImmediate,&replay::UnlockGfxImmediate}){
        unsigned char bytes[64]{};if(!safemem::ReadBytes(reinterpret_cast<void*>(base+b->rva),bytes,b->size)||memcmp(bytes,b->bytes,b->size))return hook::Status::NotReady;
    }
    return hook::Install(reinterpret_cast<void*>(base+replay::UpdateFallbackProbe.rva),&UpdateProbe,replay::UpdateFallbackProbe.bytes,replay::UpdateFallbackProbe.size,g_update);
}
}
