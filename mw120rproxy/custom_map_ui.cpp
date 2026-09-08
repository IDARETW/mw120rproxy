#include "custom_map_ui.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include <array>
#include <atomic>
#include <cstring>
#include <algorithm>

namespace {
uintptr_t g_base=0;
using LoadMaps=void(*)();
std::atomic<LoadMaps> g_load{nullptr};
constexpr size_t Stride=0x4D8,Capacity=128;
// Exact Replay Com_GameInfo_LoadMapInfoList, RVA 10C7E70..10C8437.
constexpr uintptr_t CountRva=0xC4FD8B4,ListRva=0xC4FD8B8;
void Text(unsigned char* row,size_t offset,size_t capacity,const std::string& text) {
    memset(row+offset,0,capacity);
    memcpy(row+offset,text.data(),(std::min)(capacity-1,text.size()));
}
void Load() {
    g_load.load()();
    const DWORD saved=GetLastError();
    int count=0;
    if(!safemem::ReadBytes(reinterpret_cast<void*>(g_base+CountRva),&count,4) || count<=0 || count>Capacity)return;
    auto* rows=reinterpret_cast<unsigned char*>(g_base+ListRva);
    std::array<unsigned char,Stride> reference{};
    bool found=false;
    for(int i=0;i<count;++i) {
        std::array<unsigned char,Stride> row{};
        if(!safemem::ReadBytes(rows+i*Stride,row.data(),row.size()))return;
        if(memcmp(row.data()+0x20,"mp_shipment\0",12)==0){reference=row;found=true;break;}
    }
    if(!found){LOG_WARN("Maps","native map list has no Shipment template; custom menu entries deferred");return;}
    unsigned added=0;
    for(const auto& package:custommaps::List()) {
        if(!package.valid || package.id.size()>=16 || count>=Capacity)continue;
        bool duplicate=false;
        for(int i=0;i<count;++i)if(memcmp(rows+i*Stride+0x20,package.id.c_str(),package.id.size()+1)==0){duplicate=true;break;}
        if(duplicate)continue;
        auto row=reference;
        Text(row.data(),0,32,std::string(1,char(31))+package.title);
        Text(row.data(),0x20,16,package.id);
        Text(row.data(),0x30,32,std::string(1,char(31))+"Custom CoD4 map");
        Text(row.data(),0x50,32,"mw120r/"+package.id);
        Text(row.data(),0x70,32,"mw120r/"+package.id);
        Text(row.data(),0xB0,32,"war");
        Text(row.data(),0xD0,1024,"war");
        const int aliens=0;memcpy(row.data()+0x4D0,&aliens,4);
        memcpy(rows+count*Stride,row.data(),Stride);
        ++count;++added;
        LOG_INFO("Maps","added normal Local Play map entry id=%s title=%s",package.id.c_str(),package.title.c_str());
    }
    memcpy(reinterpret_cast<void*>(g_base+CountRva),&count,4);
    LOG_INFO("Maps","normal map selector ready: custom=%u total=%d",added,count);
    SetLastError(saved);
}
}
namespace custommapui {
hook::Status Install(uintptr_t base) {
    g_base=base;
    return hook::Install(reinterpret_cast<void*>(base+replay::LoadMapInfoList.rva),&Load,
        replay::LoadMapInfoList.bytes,replay::LoadMapInfoList.size,g_load);
}
void SyncSelection(const char* map) {
    if(!map || !*map)return;
    const auto active=custommaps::Active();
    if(active==map)return;
    if(custommaps::IsKnownMap(map)) {
        if(custommaps::Select(map))LOG_INFO("Maps","normal UI selected custom package %s",map);
    } else if(!active.empty())custommaps::ClearSelection();
}
}
