#include "custom_omnvars.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace {
using Load = uintptr_t(*)(unsigned char,const char*,const char*);
using Clear = uintptr_t(*)();
std::atomic<Load> g_load{nullptr};
std::atomic<Clear> g_clear{nullptr};
std::atomic<bool> g_customTables{false};
uintptr_t g_base=0;
unsigned Count(uintptr_t rva) {
    unsigned result=0;
    safemem::ReadBytes(reinterpret_cast<void*>(g_base+rva),&result,sizeof(result));
    return result;
}
uintptr_t ClearTables() {
    // Track the native lifecycle as well as our narrow recovery call.
    g_customTables.store(false,std::memory_order_release);
    return g_clear.load(std::memory_order_acquire)();
}
uintptr_t LoadTables(unsigned char mode,const char* gameType,const char* mapName) {
    const DWORD saved=GetLastError();
    char map[96]{},type[64]{};
    const auto length=safemem::ReadString(mapName,map,sizeof(map));
    safemem::ReadString(gameType,type,sizeof(type));
    bool selected=false,custom=false;
    try {
        const auto active=custommaps::Active();
        selected=!active.empty();
        custom=mode==2 && length && length<sizeof(map)-1 && active==map;
    } catch(...) {}
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const unsigned before=Count(0x5C48518),archived=Count(0x5C4852C);
    if(mode==2 && selected && g_customTables.load(std::memory_order_acquire) && before &&
       caller==g_base+0x137B1CE && std::strcmp(map,"mp_frontend3")==0) {
        // Captured error recovery re-enters frontend server initialization without
        // clearing the prior custom match's definitions. Invoke the exact native
        // reset before this existing load; never raise limits or append twice.
        LOG_WARN("Omnvars","frontend recovery retains custom tables: clearing native state (%u definitions, %u archived)",before,archived);
        SetLastError(saved);ClearTables();
    }
    if(custom)g_customTables.store(true,std::memory_order_release);
    LOG_INFO("Omnvars","LoadTables mode=%u map='%s' gametype='%s' caller=0x%llX before=%u archived=%u custom=%u",
        mode,map,type,caller>=g_base ? caller-g_base : caller,Count(0x5C48518),Count(0x5C4852C),custom);
    SetLastError(saved);
    const auto result=g_load.load(std::memory_order_acquire)(mode,gameType,mapName);
    const DWORD after=GetLastError();
    LOG_INFO("Omnvars","LoadTables complete map='%s' definitions=%u archived=%u",map,Count(0x5C48518),Count(0x5C4852C));
    SetLastError(after);return result;
}
}
namespace customomnvars {
hook::Status Install(uintptr_t base) {
    g_base=base;
    unsigned char bytes[32]{};
    if(!safemem::ReadBytes(reinterpret_cast<void*>(base+replay::FrontendOmnvarCall.rva),bytes,replay::FrontendOmnvarCall.size) ||
       std::memcmp(bytes,replay::FrontendOmnvarCall.bytes,replay::FrontendOmnvarCall.size)!=0)return hook::Status::NotReady;
    auto status=hook::Install(reinterpret_cast<void*>(base+replay::OmnvarClear.rva),&ClearTables,
        replay::OmnvarClear.bytes,replay::OmnvarClear.size,g_clear);
    if(status!=hook::Status::Installed)return status;
    return hook::Install(reinterpret_cast<void*>(base+replay::OmnvarLoad.rva),&LoadTables,
        replay::OmnvarLoad.bytes,replay::OmnvarLoad.size,g_load);
}
}
