#include "custom_images.h"
#include "custom_maps.h"
#include "replay_bindings.h"
#include "safemem.h"
#include "logger.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <cstring>
#include <deque>

namespace {
uintptr_t g_base=0;
using Find=void*(*)(int,const char*,int);
std::atomic<Find> g_find{nullptr};
// These non-streamed code images, like the engine's code images, survive zone
// unloads. Stable addresses prevent LUI caches pointing into an unloaded map.
struct Image {
    alignas(16) std::array<unsigned char,0xE8> header{};
    alignas(16) std::array<unsigned char,0x78> material{};
    std::vector<unsigned char> textures;
    std::string name,materialName;
};
std::map<std::string,std::unique_ptr<Image>> g_images;
// Exact Replay SEH_StringEd_GetString consumes LocalizeEntry.value at +8.
struct Localized {const char* name;const char* value;std::string key,text;};
std::map<std::string,std::unique_ptr<Localized>> g_localized;
void LocalizedTitle(const std::string& key,const std::string& text){
    if(g_localized.contains(key))return;
    auto entry=std::make_unique<Localized>();entry->key=key;entry->text=text;
    entry->name=entry->key.c_str();entry->value=entry->text.c_str();g_localized.emplace(key,std::move(entry));
}
std::mutex g_mutex;
std::atomic<unsigned> g_aliasLogs{0};
// Verified against the live 1.20 Replay asset: uint16 cell indices into
// separate hash/string dictionaries. This is NOT the older 24-byte table ABI.
struct Table {const char* name;int columns,rows,unique,padding;uint16_t* indices;uint32_t* hashes;const char** strings;};
static_assert(sizeof(Table)==48);
struct MapInfo {
    const void* source=nullptr;uint16_t* sourceCells=nullptr;
    Table table{};std::vector<uint16_t> indices;std::vector<uint32_t> hashes;
    std::vector<const char*> pointers;std::deque<std::string> strings;
};
std::vector<std::unique_ptr<MapInfo>> g_mapInfo;
uint32_t StringHash(const std::string& text){uint32_t h=0;for(unsigned char c:text)h=h*31+(c>='A'&&c<='Z'?c+32:c);return h;}
bool ReadArray(const void* source,void* dest,size_t size){
    auto* in=static_cast<const unsigned char*>(source);auto* out=static_cast<unsigned char*>(dest);
    while(size){size_t available=0;if(!safemem::RegionReadable(in,available)||!available)return false;
        const size_t n=(std::min)(size,available);memcpy(out,in,n);in+=n;out+=n;size-=n;}return true;
}
void* MapInfoTable(void* source){
    Table original{};
    if(!source||!safemem::ReadBytes(source,&original,sizeof(original))||original.columns<23||original.columns>128||original.rows<1||original.rows>1024||original.unique<1||original.unique>65500||!original.indices||!original.hashes||!original.strings){LOG_WARN("MapImages","mapInfo rejected: incompatible compressed table");return source;}
    std::lock_guard lock(g_mutex);
    for(const auto& cached:g_mapInfo)if(cached->source==source&&cached->sourceCells==original.indices)return &cached->table;
    auto copy=std::make_unique<MapInfo>();copy->source=source;copy->sourceCells=original.indices;copy->table=original;
    copy->indices.resize(size_t(original.columns)*original.rows);copy->hashes.resize(original.unique);copy->pointers.resize(original.unique);
    if(!ReadArray(original.indices,copy->indices.data(),copy->indices.size()*2)||!ReadArray(original.hashes,copy->hashes.data(),copy->hashes.size()*4)||!ReadArray(original.strings,copy->pointers.data(),copy->pointers.size()*8)){LOG_WARN("MapImages","mapInfo arrays unreadable");return source;}
    for(auto index:copy->indices)if(index>=original.unique){LOG_WARN("MapImages","mapInfo index invalid");return source;}
    for(size_t i=0;i<copy->pointers.size();++i){char text[4096]{};
        if(copy->pointers[i]){auto n=safemem::ReadString(copy->pointers[i],text,sizeof(text));if(n>=sizeof(text)-1)return source;}
        if(copy->hashes[i]!=StringHash(text)){LOG_WARN("MapImages","mapInfo dictionary hash mismatch index=%zu",i);return source;}
        copy->strings.emplace_back(text);copy->pointers[i]=copy->strings.back().c_str();
    }
    auto intern=[&](const std::string& text)->uint16_t{for(size_t i=0;i<copy->pointers.size();++i)if(text==copy->pointers[i])return uint16_t(i);
        copy->strings.push_back(text);copy->pointers.push_back(copy->strings.back().c_str());copy->hashes.push_back(StringHash(text));return uint16_t(copy->pointers.size()-1);};
    auto rowName=[&](int row){return copy->pointers[copy->indices[row*original.columns]];};
    int templateRow=-1;
    for(int r=0;r<original.rows;++r)if(!strcmp(rowName(r),"mp_hackney_yard")){templateRow=r;break;}
    if(templateRow<0){LOG_WARN("MapImages","mapInfo missing mp_hackney_yard template");return source;}
    const std::vector<uint16_t> reference(copy->indices.begin()+templateRow*original.columns,copy->indices.begin()+(templateRow+1)*original.columns);
    unsigned added=0;
    for(const auto& package:custommaps::List())if(package.valid){
        if(copy->pointers.size()>65529)break;
        int row=-1;for(int r=0;r<copy->table.rows;++r)if(package.id==rowName(r)){row=r;break;}
        if(row<0){row=copy->table.rows++;copy->indices.insert(copy->indices.end(),reference.begin(),reference.end());copy->indices[row*original.columns]=intern(package.id);}
        std::string key="MW120R/MAP_"+package.id;
        for(auto& c:key)if(c>='a'&&c<='z')c-=32;
        LocalizedTitle(key,package.title);
        const auto title=intern(key);
        std::string caps=package.title;for(auto& c:caps)if(c>='a'&&c<='z')c-=32;
        copy->indices[row*original.columns+1]=title;
        LocalizedTitle(key+"_CAPS",caps);
        copy->indices[row*original.columns+2]=intern(key+"_CAPS");
        copy->indices[row*original.columns+3]=intern("");
        if(original.columns>24)copy->indices[row*original.columns+24]=title;
        const auto artwork=intern("mw120r/"+package.id);
        copy->indices[row*original.columns+21]=artwork;copy->indices[row*original.columns+22]=artwork;++added;
    }
    if(!added)return source;
    copy->table.name="mp/mapinfo.csv";copy->table.indices=copy->indices.data();copy->table.hashes=copy->hashes.data();copy->table.strings=copy->pointers.data();copy->table.unique=int(copy->pointers.size());
    auto* result=&copy->table;
    LOG_INFO("MapImages","mapInfo backgrounds ready: custom=%u columns=%d rows=%d strings=%d (Replay compressed table)",added,original.columns,copy->table.rows,copy->table.unique);
    g_mapInfo.push_back(std::move(copy));return result;
}
template<class T>void Put(Image& im,size_t offset,T value){memcpy(im.header.data()+offset,&value,sizeof(value));}
void* Result(Image& im,int type) {
    if(type==19)return im.header.data();
    if(!im.textures.empty())return im.material.data();
    // UI_DrawTempConnectScreen registers a MATERIAL, whereas LUI registers an
    // IMAGE. Clone the resident stock UI material, including its texture table;
    // never change the shared Hackney material or its native shader state.
    auto* stock=g_find.load()(11,"loadscreen_mp_hackney",0);
    if(!stock || !safemem::ReadBytes(stock,im.material.data(),im.material.size()))return nullptr;
    const unsigned count=im.material[0x1C];uintptr_t table=0;
    memcpy(&table,im.material.data()+0x48,8);
    if(count!=1 || !table)return nullptr;
    std::vector<unsigned char> textures(16);
    if(!safemem::ReadBytes(reinterpret_cast<void*>(table),textures.data(),16))return nullptr;
    im.materialName="loadscreen_"+im.name.substr(7);
    auto* materialName=im.materialName.c_str();auto* image=im.header.data();
    memcpy(im.material.data(),&materialName,8);
    memcpy(textures.data()+8,&image,8);im.textures=std::move(textures);
    auto* textureTable=im.textures.data();memcpy(im.material.data()+0x48,&textureTable,8);
    LOG_INFO("MapImages","loading-screen material ready name=%s",materialName);
    return im.material.data();
}
void* Lookup(int type,const char* name,int allowDefault){
    if(type==41 && name && !strncmp(name,"MW120R/MAP_",11)){
        std::lock_guard lock(g_mutex);auto it=g_localized.find(name);
        if(it!=g_localized.end())return it->second.get();
    }
    if(type==54 && name && !_stricmp(name,"mp/mapInfo.csv"))return MapInfoTable(g_find.load()(type,name,allowDefault));
    std::string loadingAlias;
    // Some Replay connect-screen paths retain the template map's loadscreen.
    // Selection is cleared by the stock-map UI seam, so this alias only applies
    // while an installed custom package is selected.
    if((type==19 || type==11) && name && !strcmp(name,"loadscreen_mp_hackney")){
        const auto active=custommaps::Active();
        if(!active.empty()){
            loadingAlias="loadscreen_"+active;name=loadingAlias.c_str();
            if(g_aliasLogs.fetch_add(1)<8)LOG_INFO("MapImages","loading-screen template redirected type=%d map=%s",type,active.c_str());
        }
    }
    if((type==19 || type==11) && name && (strncmp(name,"mw120r/",7)==0 || strncmp(name,"loadscreen_mp_",14)==0)) {
        for(const auto& package:custommaps::List())if(package.valid &&
            (name=="mw120r/"+package.id || name=="loadscreen_"+package.id)) {
            std::lock_guard lock(g_mutex);
            auto it=g_images.find(package.id);if(it!=g_images.end()){
                if(auto* result=Result(*it->second,type))return result;
                break;
            }
            const auto path=std::filesystem::path(package.directory)/"preview.rgba";
            std::ifstream input(path,std::ios::binary);unsigned header[4]{};
            if(input.read(reinterpret_cast<char*>(header),sizeof(header)) && header[0]==0x4952574D && header[1]==1 &&
                header[2]>=1 && header[2]<=2048 && header[3]>=1 && header[3]<=2048){
                std::vector<unsigned char> pixels(size_t(header[2])*header[3]*4);
                if(input.read(reinterpret_cast<char*>(pixels.data()),pixels.size()) && input.peek()==EOF){
                    auto im=std::make_unique<Image>();im->name="mw120r/"+package.id;
                    Put(*im,0,im->name.c_str());Put(*im,0x14,7u);Put(*im,0x18,3u);Put(*im,0x1C,unsigned(pixels.size()));
                    Put(*im,0x24,uint16_t(header[2]));Put(*im,0x26,uint16_t(header[3]));Put(*im,0x28,uint16_t(1));Put(*im,0x2A,uint16_t(1));
                    im->header[0x2E]=1;im->header[0x2F]=1;im->header[0x30]=1;
                    // Exact Image_LoadPixels(pixel-pointer, image) creates a
                    // resident native texture and clears the consumed CPU pointer.
                    void* data=pixels.data();Put(*im,0xE0,data);
                    reinterpret_cast<void(*)(void**,void*)>(g_base+replay::ImageLoadPixels.rva)(&data,im->header.data());
                    unsigned texture=0;memcpy(&texture,im->header.data()+0x10,4);
                    if(texture){auto* result=Result(*im,type);g_images.emplace(package.id,std::move(im));
                        LOG_INFO("MapImages","resident artwork ready map=%s dimensions=%ux%u texture=%u",package.id.c_str(),header[2],header[3],texture);if(result)return result;}
                }
            }
            break;
        }
    }
    return g_find.load()(type,name,allowDefault);
}
}
namespace customimages {
hook::Status Install(uintptr_t base){
    if(g_find.load())return hook::Status::Installed;
    g_base=base;const auto& upload=replay::ImageLoadPixels;unsigned char bytes[64]{};
    if(!safemem::ReadBytes(reinterpret_cast<void*>(base+upload.rva),bytes,upload.size)||memcmp(bytes,upload.bytes,upload.size))return hook::Status::NotReady;
    return hook::Install(reinterpret_cast<void*>(base+replay::FindAsset.rva),&Lookup,replay::FindAsset.bytes,replay::FindAsset.size,g_find);
}
}
