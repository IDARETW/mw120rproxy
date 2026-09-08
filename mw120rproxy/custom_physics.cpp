#include "custom_physics.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "asset_context.h"
#include "safemem.h"
#include "logger.h"
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace {
using AddShapes=void*(*)(char**,unsigned*,const char*,int);
std::atomic<AddShapes> g_original{nullptr};
uintptr_t g_base=0;
std::atomic<bool> g_emptyWorld{false};
bool Matches(const replay::Binding& binding) {
    unsigned char bytes[64]{};
    return safemem::ReadBytes(reinterpret_cast<void*>(g_base+binding.rva),bytes,binding.size) &&
        memcmp(bytes,binding.bytes,binding.size)==0;
}
bool EmptySelected(char** data,unsigned* size,const char* name,int type,uintptr_t caller) {
    size_t field=0,countField=0;
    if(type==29 && caller==g_base+0x1490C4F) {field=0x150;countField=0x158;}
    else if(type==23 && caller==g_base+0x1490AB0) {field=0xC0;countField=0x0C;}
    else return false;
    const auto asset=reinterpret_cast<uintptr_t>(data)-field;
    if(reinterpret_cast<uintptr_t>(size)!=asset+field-8) return false;
    const char* assetName=nullptr;unsigned count=~0u;
    if(!safemem::ReadBytes(reinterpret_cast<void*>(asset),&assetName,sizeof(assetName)) || assetName!=name ||
       !safemem::ReadBytes(reinterpret_cast<void*>(asset+countField),&count,sizeof(count)) || count) return false;
    char text[160]{};
    const auto length=safemem::ReadString(name,text,sizeof(text));
    if(!length || length==sizeof(text)-1)return false;
    try {
        const auto active=custommaps::Active();
        return !active.empty() && ("maps/mp/"+active+".d3dbsp")==text;
    } catch(...) {return false;}
}
void* AddShapeList(char** data,unsigned* size,const char* name,int type) {
    if(type==23)g_emptyWorld.store(false);
    const DWORD saved=GetLastError();
    char* payload=nullptr;unsigned bytes=0;
    const bool readable=safemem::ReadBytes(data,&payload,sizeof(payload)) && safemem::ReadBytes(size,&bytes,sizeof(bytes));
    if(assetcontext::current.active) {
        assetcontext::current.phase="HavokPhysics_AddShapeList";
        assetcontext::current.shapeData=reinterpret_cast<uintptr_t>(payload);
        assetcontext::current.shapeBytes=bytes;
    }
    if(readable && !payload && !bytes && EmptySelected(data,size,name,type,reinterpret_cast<uintptr_t>(_ReturnAddress()))) {
        if(type==23)g_emptyWorld.store(true);
        // These two verified callers accept a null shape list. Their native
        // registration/global updates still run. This represents no collision;
        // it does not fabricate a Havok object or swallow deserialization errors.
        LOG_INFO("Maps","selected prototype '%s' type=%d: no serialized Havok shapes; custom brush bodies initialize at WorldCollision creation",name,type);
        if(assetcontext::current.active)assetcontext::current.phase="empty custom physics: no shapes";
        SetLastError(saved);return nullptr;
    }
    SetLastError(saved);
    return g_original.load(std::memory_order_acquire)(data,size,name,type);
}
}
namespace customphysics {
bool OwnsEmptyWorld() { return g_emptyWorld.load(); }
hook::Status Install(uintptr_t base) {
    if(g_original.load(std::memory_order_acquire))return hook::Status::Installed;
    g_base=base;
    if(!Matches(replay::MapShapeCall) || !Matches(replay::ClipShapeCall) || !Matches(replay::SetMainShapeList))return hook::Status::NotReady;
    return hook::Install(reinterpret_cast<void*>(base+replay::AddShapeList.rva),&AddShapeList,
        replay::AddShapeList.bytes,replay::AddShapeList.size,g_original);
}
}
