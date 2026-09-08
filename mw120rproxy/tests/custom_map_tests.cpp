#include "custom_surfaces.h"
#include "command_text.h"
#include "noclip.h"
#include "custom_maps.h"
#include "custom_map_loader.h"
#include "developer_ui.h"
#include "replay_bindings.h"
#include "custom_physics.h"
#include "custom_collision.h"
#include "custom_omnvars.h"
#include "custom_render.h"
#include "custom_map_ui.h"
#include "custom_images.h"
#include "custom_audio.h"
#include "custom_ambient.h"
#include "custom_ladders.h"
#include "ladder_file.h"
#include "custom_glass.h"
#include "glass_file.h"
#include "collision_file.h"
#include "compound_collision.h"
#include "replay_compound_fixture.h"
#include "replay_convex_fixture.h"
#include "replay_metadata_fixture.h"
#include "replay_ladder_ik_fixture.h"
#include "../../iw8-zonetool/src/iw8/replay_netconst.h"
#include <cstdarg>
#include "replay_physics_fixture.h"
#include "log.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) { fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
    }
    fs::path root, packageRoot;
    const std::string id = "mp_proxy_fixture_" + std::to_string(GetCurrentProcessId());
    void Text(const fs::path& path, const std::string& text) { std::ofstream(path) << text; }
    void MakePackage(const std::string& map)
    {
        const auto dir = packageRoot / map;
        Check(!fs::exists(dir),"test package does not overwrite existing content");
        fs::create_directories(dir);
        Text(dir/"manifest.json","{\"schema\":1,\"id\":\""+map+"\",\"title\":\"Fixture\",\"gametypes\":[\"tdm\"]}");
        std::array<unsigned char,0x94> bytes{};
        memcpy(bytes.data(),"IWffc100",8);
        uint32_t header=11, version=0xFF7, resident=12, block=8;
        uint64_t size=8;
        memcpy(bytes.data()+8,&header,4); memcpy(bytes.data()+12,&version,4);
        memcpy(bytes.data()+0x14,&resident,4); memcpy(bytes.data()+0x20,&size,8);
        memcpy(bytes.data()+0x30,&block,4); memcpy(bytes.data()+0x88,"\x01IWC",4);
        for (const char* prefix : {"","srv_","eng_","ww_","techsets_"})
        {
            std::ofstream file(dir/(std::string(prefix)+map+".ff"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        }
    }
    void ParserTests()
    {
        Check(commandtext::Translate("noclip; noclip on\nmw_noclip off; cmd noclip 0") ==
            "cmd noclip; cmd noclip on\ncmd noclip off; cmd noclip 0","noclip explicitly forwards to server without double forwarding");
        Check(commandtext::Translate("echo \"noclip\"; echo noclip") == "echo \"noclip\"; echo noclip","noclip values and quoted literals unchanged");
        Check(commandtext::Translate("set ui_mapname mp_test; ui_gametype tdm\n") ==
            "set NSQLTTMRMP mp_test; MOLPOSLOMO tdm\n","translate dvar names at command boundaries");
        Check(commandtext::Translate("echo \"set ui_mapname x; ui_gametype tdm\"; set ui_gametype \"tdm\"") ==
            "echo \"set ui_mapname x; ui_gametype tdm\"; set MOLPOSLOMO \"tdm\"","preserve quoted strings and delimiters");
        Check(commandtext::Translate("// ui_mapname; set ui_gametype x\nui_mapname") ==
            "// ui_mapname; set ui_gametype x\nNSQLTTMRMP","comments do not become commands");
        Check(commandtext::Translate("echo ui_mapname\nset unknown ui_mapname") ==
            "echo ui_mapname\nset unknown ui_mapname","values and unknown names unchanged");
        Check(commandtext::Translate("set ui_mapname \"a\\\"; ui_gametype\"") ==
            "set NSQLTTMRMP \"a\\\"; ui_gametype\"","escaped quotes preserved");
        puts("PASS: quote-aware console translation, chained commands, comments and literal values");
    }
    void PackageTests()
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr,exe,MAX_PATH);
        root=fs::path(exe).parent_path(); packageRoot=root/"mods/mw120r/maps";
        MakePackage(id); MakePackage(id+"_nested"); MakePackage(id+"_large");
        Text(packageRoot/(id+"_nested")/"manifest.json",
            "{\"nested\":{\"schema\":1,\"id\":\"mp_proxy_nested\",\"title\":\"Wrong scope\",\"gametypes\":[\"tdm\"]}}");
        std::fstream large(packageRoot/(id+"_large")/(id+"_large.ff"),std::ios::binary|std::ios::in|std::ios::out);
        large.seekp(0x24); const uint32_t high=1; large.write(reinterpret_cast<const char*>(&high),4); large.close();
        custommaps::Initialize(GetModuleHandleW(nullptr));
        Check(custommaps::Select(id.c_str()),"valid structural package selected");
        Check(!custommaps::Select((id+"_nested").c_str()),"nested manifest keys cannot satisfy top-level schema");
        Check(!custommaps::Select((id+"_large").c_str()),"64-bit inflated size cannot evade block-size validation");
        std::string target;
        Check(custommaps::ResolveDiskRead(("zone/"+id+".ff").c_str(),target),"selected root zone resolves");
        Check(fs::path(target)==packageRoot/id/(id+".ff"),"redirect points at exact package file");
        Check(custommaps::ResolveDiskRead((root/"zone/worldwide"/("ww_"+id+".ff")).string().c_str(),target),"absolute worldwide zone resolves");
        Check(!custommaps::ResolveDiskRead("zone/mp_shipment.ff",target),"stock zones do not redirect");
        Check(!custommaps::ResolveDiskRead(("zone/../zone/"+id+".ff").c_str(),target),"parent traversal rejected");
        Check(!custommaps::ResolveDiskRead(("unrelated/"+id+".ff").c_str(),target),"unrelated locations do not alias by basename");
        Check(!custommaps::ResolveDiskRead(("zone/"+id+".fp").c_str(),target),"patch companions are not fabricated");
        Check(!custommaps::ResolveDiskRead((root.parent_path()/"other"/(id+".ff")).string().c_str(),target),"external absolute paths stay unchanged");
        const auto shader=root/"zone/techsets_mp_frontend3.ff";
        Check(!fs::exists(shader),"shader fixture cannot overwrite existing data");
        fs::create_directories(shader.parent_path());
        Text(packageRoot/id/"manifest.json","{\"schema\":1,\"id\":\""+id+"\",\"title\":\"Fixture\",\"gametypes\":[\"tdm\"],\"shaderSource\":\"mp_frontend3\"}");
        custommaps::Refresh();Check(!custommaps::Select(id.c_str()),"missing shader dependency is rejected");
        const unsigned char signedHeader[]{'I','W','f','f','a','1','0','0',11,0,0,0,0xF7,0x0F,0,0,1,0,0,0};
        {std::ofstream f(shader,std::ios::binary);f.write(reinterpret_cast<const char*>(signedHeader),sizeof(signedHeader));}
        custommaps::Refresh();Check(custommaps::Select(id.c_str()),"explicit installed shader dependency accepted");
        Check(custommaps::ResolveDiskRead(("zone/techsets_"+id+".ff").c_str(),target)&&fs::path(target)==shader,"selected techsets use declared stock source");
        Check(!custommaps::ResolveDiskRead("zone/techsets_mp_shipment.ff",target),"other map shaders remain native");
        std::wstring shaderPath;char shaderQPath[256]{};
        Check(custommaps::ActiveZonePath(("techsets_"+id).c_str(),shaderPath)&&fs::path(shaderPath)==shader,"decoder sees actual signed file");
        Check(custommaps::ActiveZoneQPath(("techsets_"+id).c_str(),shaderQPath,sizeof(shaderQPath))&&std::string(shaderQPath)=="zone/techsets_mp_frontend3.ff","FS reader uses same dependency");
        fs::remove(shader);
        Text(packageRoot/id/"manifest.json","{\"schema\":1,\"schema\":1,\"id\":\""+id+"\",\"title\":\"Fixture\",\"gametypes\":[\"tdm\"]}");
        custommaps::Refresh(); Check(!custommaps::Select(id.c_str()),"duplicate schema rejected");
        Text(packageRoot/id/"manifest.json","{\"schema\":1,\"id\":\""+id+"\",\"title\":\"Fixture\",\"gametypes\":[\"tdm\"]}");
        custommaps::Refresh(); Check(custommaps::Select(id.c_str()),"refresh restores valid package");
        puts("PASS: package schema, 64-bit size, exact family routing, stock passthrough and path containment");
    }
    unsigned char* image;
    void Commit(uintptr_t rva)
    {
        Check(VirtualAlloc(image+(rva&~uintptr_t{0xFFF}),0x1000,MEM_COMMIT,PAGE_EXECUTE_READWRITE)!=nullptr,"commit sparse fixture page");
    }
    void Jump(void* location, const void* destination)
    {
        const unsigned char instruction[]={0xFF,0x25,0,0,0,0};
        memcpy(location,instruction,6); memcpy(static_cast<char*>(location)+6,&destination,8);
    }
    template<class Fn> void Stub(const replay::Binding& binding, Fn function)
    { Jump(image+binding.rva,reinterpret_cast<const void*>(function)); }
    void PrologueFixture(const replay::Binding& binding,const std::vector<unsigned char>& undo,const void* callback)
    {
        memcpy(image+binding.rva,binding.bytes,binding.size);
        memcpy(image+binding.rva+binding.size,undo.data(),undo.size());
        Jump(image+binding.rva+binding.size+undo.size(),callback);
    }
    std::string openedName;
    unsigned openedFlags;
    HANDLE __fastcall NativeDisk(const char* name,unsigned flags)
    {
        openedName=name; openedFlags=flags;
        auto handle=CreateFileA(name,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        SetLastError(0x4567); return handle;
    }
    bool mainThread=true, frontend=true, console=false;
    unsigned outputToggles=0, originalResets=0, inflatorInits=0;
    uintptr_t expectedState=0, expectedDescriptor=0, expectedOptional=0;
    unsigned expectedInitKind=0;
    std::string expectedAuthName;
    uintptr_t NativeReset(uintptr_t){++originalResets;SetLastError(0x5678);return 55;}
    uintptr_t NativeInit(uintptr_t state,unsigned kind,uintptr_t descriptor,uintptr_t optional)
    {
        const bool nameMatches=expectedAuthName.empty()?descriptor==expectedDescriptor:
            strcmp(reinterpret_cast<const char*>(descriptor),expectedAuthName.c_str())==0;
        Check(state==expectedState&&kind==expectedInitKind&&nameMatches&&optional==expectedOptional,"selected decoder ABI and authenticated source name");
        ++inflatorInits;SetLastError(0x6789);return 66;
    }
    void ToggleOutput(){++outputToggles;}
    int forwardedKeys=0, drawCalls=0, lastClient=-1, lastVirtual=-1, lastController=-1;
    unsigned lastTime=0;
    std::string nativeMap="mp_shipment", nativeMode="war", lastCommand;
    int criticalDepth=0;
    bool IsMain(){return mainThread;} bool Frontend(){return frontend;}
    bool Bool(const char*){return true;} bool Active(int){return console;}
    void Toggle(){console=!console;} void DrawConsole(int){++drawCalls;}
    const char* String(const char* name){return strcmp(name,"NSQLTTMRMP")==0?nativeMap.c_str():nativeMode.c_str();}
    void Enter(int n){Check(n==35,"native cbuf critical section");++criticalDepth;}
    void Leave(int n){Check(n==35&&criticalDepth>0,"native cbuf lock paired");--criticalDepth;}
    uintptr_t NativeOverlay(int client);
    void NativeKey(int client,int,bool,unsigned time,int vk,int controller)
    {++forwardedKeys;lastClient=client;lastTime=time;lastVirtual=vk;lastController=controller;}
    void NativeAdd(int,const char* text)
    {
        lastCommand=text;
        if (lastCommand.starts_with("set NSQLTTMRMP "))
        {
            const auto end=lastCommand.find('\n');
            nativeMap=lastCommand.substr(15,end-15);
            const auto mode=lastCommand.find("set MOLPOSLOMO ");
            if(mode!=std::string::npos)
                nativeMode=lastCommand.substr(mode+15,lastCommand.find('\n',mode)-(mode+15));
        }
    }
    uintptr_t NativeOverlay(int client)
    {
        // Model the real next-frame consumer, independently pinned to local
        // client zero. The old debug append routine used the next slot.
        struct Buffer{char*data;int capacity;int size;};
        auto* buffer=reinterpret_cast<Buffer*>(image+0xD216D70);
        if(buffer->data && buffer->size>0) {
            Check(buffer->capacity==0x10000 && buffer->size<buffer->capacity,"local Cbuf bounds");
            Check(buffer->data[buffer->size]=='\0',"native Cbuf payload is terminated");
            NativeAdd(client,buffer->data);buffer->size=0;
        }
        lastClient=client;return 0x12345678;
    }
    void* FindAsset(int,const char*,int){return nullptr;}
    void DrawText(const void*,const char*,int,const void*,float,float,int,int,float,float,const float*,int){}
    void DrawPic(float,float,float,float,float,float,float,float,const float*,const void*){}
    void RuntimeTests()
    {
        image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1324B000,MEM_RESERVE,PAGE_NOACCESS));
        Check(image!=nullptr,"reserve Replay fixture");
        for(const auto* b:{&replay::Overlay,&replay::KeyEvent,&replay::AddConsoleText,&replay::DiskOpen,&replay::DrawText,
             &replay::DrawPic,&replay::FindAsset,&replay::DrawConsole,&replay::ToggleConsole,&replay::ToggleOutput,&replay::ConsoleActive,
             &replay::ResetReader,&replay::InitInflator,
             &replay::IsMainThread,&replay::IsFrontEnd,&replay::GetString,&replay::GetBool,&replay::EnterCritical,&replay::LeaveCritical})
        {Commit(b->rva);memcpy(image+b->rva,b->bytes,b->size);}
        Commit(0x15F4CC0); Jump(image+0x15F4CC0,reinterpret_cast<const void*>(&NativeOverlay));
        Commit(replay::UIFont); Commit(replay::ConsoleBuffer);
        std::array<char,0x10000> commandBuffer{};
        struct Buffer{char*data;int capacity;int size;} buffer{commandBuffer.data(),0x10000,0};
        memcpy(image+replay::ConsoleBuffer,&buffer,sizeof(buffer));
        PrologueFixture(replay::DiskOpen,{0x48,0x8B,0x5C,0x24,0x50,0x48,0x83,0xC4,0x40,0x5F},reinterpret_cast<const void*>(&NativeDisk));
        PrologueFixture(replay::KeyEvent,{0x48,0x8B,0x5C,0x24,0x28,0x48,0x8B,0x6C,0x24,0x38,0x41,0x5C,0x5F,0x5E},reinterpret_cast<const void*>(&NativeKey));
        PrologueFixture(replay::AddConsoleText,{0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x74,0x24,0x38,0x48,0x83,0xC4,0x20,0x5F},reinterpret_cast<const void*>(&NativeAdd));
        PrologueFixture(replay::ResetReader,{0x48,0x83,0xC4,0x20,0x5B},reinterpret_cast<const void*>(&NativeReset));
        Check(customloader::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"native disk hook installed");
        Check(developerui::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"native UI hooks installed");
        Check(developerui::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"UI installation idempotent");
        // Non-hook callees are replaced only in this disposable fixture after byte validation.
        Stub(replay::IsMainThread,&IsMain);Stub(replay::IsFrontEnd,&Frontend);Stub(replay::GetBool,&Bool);
        Stub(replay::ConsoleActive,&Active);Stub(replay::ToggleConsole,&Toggle);Stub(replay::DrawConsole,&DrawConsole);
        Stub(replay::GetString,&String);Stub(replay::EnterCritical,&Enter);Stub(replay::LeaveCritical,&Leave);
        Stub(replay::FindAsset,&FindAsset);Stub(replay::DrawText,&DrawText);Stub(replay::DrawPic,&DrawPic);
        Stub(replay::InitInflator,&NativeInit);Stub(replay::ToggleOutput,&ToggleOutput);
        FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
        auto disk=reinterpret_cast<HANDLE(*)(const char*,unsigned)>(image+replay::DiskOpen.rva);
        const auto request=(root/"zone"/(id+".ff")).string();
        HANDLE handle=disk(request.c_str(),0x19);
        Check(handle!=INVALID_HANDLE_VALUE&&openedFlags==0x19&&GetLastError()==0x4567,"native handle flags and last error survive redirect");
        char magic[8]{};DWORD read=0;
        Check(ReadFile(handle,magic,8,&read,nullptr)&&read==8&&memcmp(magic,"IWffc100",8)==0,"returned handle supports native reads");
        Check(CloseHandle(handle),"returned handle supports native close");
        const std::string stock="zone/mp_shipment.ff";
        handle=disk(stock.c_str(),0x1);
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
        Check(openedName==stock&&openedFlags==1,"unrelated native path and flags unchanged");
        std::array<unsigned char,0x120> descriptor{}, reader{};
        std::array<unsigned char,32> state{};
        expectedState=reinterpret_cast<uintptr_t>(state.data());expectedDescriptor=reinterpret_cast<uintptr_t>(descriptor.data());
        memcpy(reader.data(),&expectedState,8);memcpy(reader.data()+0xB0,&expectedDescriptor,8);
        memcpy(descriptor.data(),(id+".ff").c_str(),id.size()+4);
        std::ifstream source(packageRoot/id/(id+".ff"),std::ios::binary);
        source.read(reinterpret_cast<char*>(descriptor.data()+0x88),0x88);
        auto reset=reinterpret_cast<uintptr_t(*)(uintptr_t)>(image+replay::ResetReader.rva);
        Check(reset(reinterpret_cast<uintptr_t>(reader.data()))==66&&inflatorInits==1&&GetLastError()==0x6789,"selected stored package uses native unsigned decoder");
        descriptor[0x54]=1;expectedOptional=expectedDescriptor+0x54;
        Check(reset(reinterpret_cast<uintptr_t>(reader.data()))==66&&inflatorInits==2,"optional compression state preserved");
        descriptor[0x50]=1;
        Check(reset(reinterpret_cast<uintptr_t>(reader.data()))==55&&originalResets==1&&GetLastError()==0x5678,"signed reader follows original reset");
        descriptor[0x50]=0;descriptor[0x88+12]=0;
        Check(reset(reinterpret_cast<uintptr_t>(reader.data()))==55&&originalResets==2,"unsupported header follows original reset");
        descriptor[0x88+12]=0xF7;
        const auto shader=root/"zone/techsets_mp_frontend3.ff";
        const unsigned char signedHeader[]{'I','W','f','f','a','1','0','0',11,0,0,0,0xF7,0x0F,0,0,1,0,0,0};
        Check(!fs::exists(shader),"auth fixture cannot overwrite installed data");
        {std::ofstream f(shader,std::ios::binary);f.write(reinterpret_cast<const char*>(signedHeader),sizeof(signedHeader));}
        Text(packageRoot/id/"manifest.json","{\"schema\":1,\"id\":\""+id+"\",\"title\":\"Fixture\",\"gametypes\":[\"tdm\"],\"shaderSource\":\"mp_frontend3\"}");
        custommaps::Refresh();Check(custommaps::Select(id.c_str()),"auth test selects declared dependency");
        strcpy_s(reinterpret_cast<char*>(descriptor.data()),64,("techsets_"+id+".ff").c_str());
        descriptor[0x50]=1;memcpy(descriptor.data()+0x88,signedHeader,sizeof(signedHeader));
        expectedInitKind=1;expectedAuthName="techsets_mp_frontend3.ff";
        Check(reset(reinterpret_cast<uintptr_t>(reader.data()))==66&&inflatorInits==3,"dependency retains signed decoder with original authenticated name");
        strcpy_s(reinterpret_cast<char*>(descriptor.data()),64,"techsets_mp_stock.ff");
        Check(reset(reinterpret_cast<uintptr_t>(reader.data()))==55&&originalResets==3,"unrelated signed reader does not inherit dependency name");
        fs::remove(shader);
        Text(packageRoot/id/"manifest.json","{\"schema\":1,\"id\":\""+id+"\",\"title\":\"Fixture\",\"gametypes\":[\"tdm\"]}");
        custommaps::Refresh();
        strcpy_s(reinterpret_cast<char*>(descriptor.data()),64,(id+".ff").c_str());
        source.clear();source.seekg(0);source.read(reinterpret_cast<char*>(descriptor.data()+0x88),0x88);
        descriptor[0x50]=0;expectedInitKind=0;expectedAuthName.clear();
        custommaps::ClearSelection();
        Check(reset(reinterpret_cast<uintptr_t>(reader.data()))==55&&originalResets==4,"unselected unsigned reader follows original reset");
        auto key=reinterpret_cast<void(*)(int,int,bool,unsigned,int,int)>(image+replay::KeyEvent.rva);
        auto frame=reinterpret_cast<uintptr_t(*)(uintptr_t,int)>(image+replay::Overlay.rva);
        auto add=reinterpret_cast<void(*)(int,const char*)>(image+replay::AddConsoleText.rva);
        Check(frame(123,0)==0x12345678&&drawCalls==1&&lastClient==0,"thin overlay trampoline preserves local client and native return");
        key(1,42,true,987,654,321);
        Check(lastClient==1&&lastTime==987&&lastVirtual==654&&lastController==321,"all six key arguments forwarded");
        key(0,96,true,0,VK_OEM_3,0);Check(console,"grave explicitly opens console");
        key(0,96,true,0,VK_OEM_3,0);Check(console,"grave autorepeat stays open");
        key(0,96,false,0,VK_OEM_3,0);
        key(0,0xA0,true,0,VK_F7,0);key(0,0xA0,false,0,VK_F7,0);Check(!console,"F7 closes console");
        key(0,140,true,0,VK_SHIFT,0);
        key(0,96,true,0,VK_OEM_3,0);key(0,96,false,0,VK_OEM_3,0);
        key(0,140,false,0,VK_SHIFT,0);Check(console&&outputToggles==1,"shift grave opens expanded console");
        key(0,0xA0,true,0,VK_F7,0);key(0,0xA0,false,0,VK_F7,0);Check(!console,"F7 toggles after shift release");
        key(0,0x9F,true,0,0,0); Check(console,"F6 acquires console input catcher");
        const int before=forwardedKeys;
        key(0,0x9F,true,0,0,0);Check(console,"F6 autorepeat does not toggle again");
        key(0,0x9F,false,0,0,0);Check(forwardedKeys==before,"consumed key-up is paired");
        key(0,'w',false,5,6,7);Check(forwardedKeys==before+1,"previously held gameplay key can release");
        // Failed prior test processes can leave their own uniquely named fixtures.
        // Select this process's row instead of assuming it sorts first.
        for(const auto& package:custommaps::List()) {
            if(package.id==id)break;
            key(0,133,true,0,0,0);key(0,133,false,0,0,0);
        }
        key(0,13,true,0,0,0);key(0,13,false,0,0,0);
        frame(123,0);
        Check(custommaps::Active()==id&&nativeMap==id&&nativeMode=="war"&&criticalDepth==0,"browser selection uses native Cbuf, engine TDM name and selected package");
        frame(123,0);
        frontend=false;
        key(0,8,true,0,0,0);key(0,8,false,0,0,0);
        Check(custommaps::Active()==id,"map switching is blocked outside frontend");
        frontend=true;
        key(0,8,true,0,0,0);key(0,8,false,0,0,0);
        frame(123,0);
        Check(custommaps::Active().empty()&&nativeMap=="mp_shipment","stock restoration clears routing");
        key(0,27,true,0,0,0);key(0,27,false,0,0,0);Check(!console,"escape releases catcher");
        add(0,"set ui_mapname mp_test");frame(123,0);Check(lastCommand=="set NSQLTTMRMP mp_test","native console alias translation");
        add(1,"ui_mapname");Check(lastCommand=="ui_mapname","other client console untouched");
        mainThread=false;const auto old=drawCalls;frame(0,0);Check(drawCalls==old,"render and Cbuf callbacks gated to main thread");
        mainThread=true;buffer.size=0xFFFF;memcpy(image+replay::ConsoleBuffer,&buffer,sizeof(buffer));
        key(0,0x9F,true,0,0,0);key(0,0x9F,false,0,0,0);
        key(0,13,true,0,0,0);key(0,13,false,0,0,0);
        Check(custommaps::Active().empty(),"full native command buffer rolls back selection");
        puts("PASS: real-prologue native disk handles, overlay and six-argument key ABI, input lifecycle, Cbuf routing, frontend gate and overflow rollback");
    }
    unsigned visibilityCalls=0,queryCalls=0,expectedVisibility=0;
    void NativeVisibility(unsigned view) {Check(view==0,"camera visibility ABI");++visibilityCalls;SetLastError(0x9999);}
    void NativeQuery(const void* command) {
        Check(command && visibilityCalls==expectedVisibility,"fallback precedes native query completion");
        Check(GetLastError()==0x1234,"visibility wrapper preserves incoming LastError");++queryCalls;
    }
    unsigned nativeDrawCalls=0;
    void NativeBspDraw(const void*) {++nativeDrawCalls;SetLastError(0x3456);}
    void NativeBspDispatch(void*,const void*) {++nativeDrawCalls;SetLastError(0x4567);}
    void RenderTests() {
        for(uintptr_t rva:{0x18CDD20,0x18F1EE0,0x10C77870,0x18D19C0,0x18F9460})Commit(rva);
        PrologueFixture(replay::UmbraQueryStaticVisibilityCmd,{0x41,0x5C,0x5F,0x5D},reinterpret_cast<const void*>(&NativeQuery));
        PrologueFixture(replay::UmbraSetAllVisible,{},reinterpret_cast<const void*>(&NativeVisibility));
        PrologueFixture(replay::AddBspDrawSurfacesCamera,{0x48,0x81,0xC4,0xB0,0,0,0,0x5D},reinterpret_cast<const void*>(&NativeBspDraw));
        PrologueFixture(replay::DrawBspSurf,{0x48,0x8B,0x74,0x24,0x60,0x48,0x83,0xC4,0x40,0x5F},reinterpret_cast<const void*>(&NativeBspDispatch));
        Check(customrender::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"checked visibility hook installed");
        reinterpret_cast<void(*)(const void*)>(image+replay::AddBspDrawSurfacesCamera.rva)(nullptr);
        Check(nativeDrawCalls==1&&GetLastError()==0x3456,"BSP diagnostic forwards native call and LastError");
        uintptr_t context[2]{};
        reinterpret_cast<void(*)(void*,const void*)>(image+replay::DrawBspSurf.rva)(nullptr,context);
        Check(nativeDrawCalls==2&&GetLastError()==0x4567,"BSP dispatch diagnostic forwards native call and LastError");
        std::array<unsigned char,0x4590> world{};
        std::array<unsigned char,0xA0> command{};
        const char* name="maps/mp/mp_test.d3dbsp";memcpy(world.data(),&name,8);
        auto ptr=world.data();memcpy(image+0x10C77870,&ptr,8);
        MakePackage("mp_test");custommaps::Refresh();Check(custommaps::Select("mp_test"),"select visibility fixture");
        auto query=reinterpret_cast<void(*)(const void*)>(image+0x18CDD20);
        const auto run=[&](){SetLastError(0x1234);query(command.data());};
        expectedVisibility=1;run();
        command[0x7C]=1;run();command[0x7C]=0;
        world[0x4460]=1;run();world[0x4460]=0;
        name="maps/mp/mp_shipment.d3dbsp";memcpy(world.data(),&name,8);run();
        name="maps/mp/mp_test.d3dbsp";memcpy(world.data(),&name,8);
        custommaps::ClearSelection();run();
        Check(visibilityCalls==1 && queryCalls==5,"only first job of selected custom world without a tome uses fallback");
        MakePackage("mp_4doffice");MakePackage("mp_nuketown");custommaps::Refresh();
        Check(custommaps::Select("mp_4doffice"),"select second map");
        name="maps/mp/mp_4doffice.d3dbsp";memcpy(world.data(),&name,8);expectedVisibility=2;run();
        Check(custommaps::Select("mp_nuketown"),"select third map");
        run(); // The old office world must not match the newly selected map.
        name="maps/mp/mp_nuketown.d3dbsp";memcpy(world.data(),&name,8);expectedVisibility=3;run();
        Check(visibilityCalls==3 && queryCalls==8,"visibility follows selected map and rejects stale world");
        custommaps::ClearSelection();
        fs::remove_all(packageRoot/"mp_4doffice");fs::remove_all(packageRoot/"mp_nuketown");
        // This fixed-name disposable package was created above only if absent.
        fs::remove_all(packageRoot/"mp_test");custommaps::Refresh();
        puts("PASS: native visibility hook ABI and completion order; later jobs, real tomes, stock worlds and unselected maps pass through");
    }
    unsigned deserializeCalls=0,clearMainCalls=0,setMainCalls=0,physicsLocks=0;
    void* lastRaw=nullptr;unsigned lastRawSize=0;
    std::array<unsigned char,0x80> shapeList{};
    void* BeginReflection(void* self){return self;}
    void EndReflection(void*){}
    struct ReflectVar{void* address;void* type;uintptr_t implementation;};
    void* Deserialize(void*,ReflectVar* result,void* raw,unsigned size,void* type){
        ++deserializeCalls;lastRaw=raw;lastRawSize=size;
        *result={shapeList.data(),type,0};return result;
    }
    bool TypeMatches(void*,void*){return true;}
    void WINAPI PhysicsEnter(void*){++physicsLocks;}
    void WINAPI PhysicsLeave(void*){Check(physicsLocks==1,"physics critical section paired");--physicsLocks;}
    void ClearMain(void*){++clearMainCalls;}
    void SetMain(void*,void* data){Check(data==shapeList.data()+0x68,"native main-shape list argument");++setMainCalls;}
    template<size_t N> void NativeBytes(uintptr_t rva,const unsigned char(&bytes)[N]){Commit(rva);Commit(rva+N-1);memcpy(image+rva,bytes,N);}
    void* HullConfig(void* config){memset(config,0,80);return config;}
    int HullThread(){return 0;}
    void HullConfigDestroy(void*){}
    unsigned hullBuildCalls=0;
    void* HullBuild(void* raw,float radius,void* config) {
        struct Strided {float* points;int count,stride;};auto& v=*static_cast<Strided*>(raw);
        Check(v.count==4&&v.stride==16&&radius==0,"native convex ABI count/stride/radius");
        Check(v.points[0]==1&&v.points[4]==2&&v.points[8]==3&&v.points[12]==4,"native convex preserves point storage");
        Check(*reinterpret_cast<unsigned*>(static_cast<char*>(config)+0x48)==252,"native convex vertex limit");
        Check(*static_cast<float*>(config)==0&&static_cast<unsigned char*>(config)[4]==0,"native hull preserves planes without radius shrink");
        ++hullBuildCalls;return reinterpret_cast<void*>(0x12345678);
    }
    void ConvexNativeTests() {
        NativeBytes(0x161CBF0,ReplayConvexCode);
        for(uintptr_t rva:{0x1E81300,0x108F210,0xF04F710,0x1E82060,0x1C8E380})Commit(rva);
        Jump(image+0x1E81300,reinterpret_cast<const void*>(&HullConfig));
        Jump(image+0x108F210,reinterpret_cast<const void*>(&HullThread));
        Jump(image+0x1E82060,reinterpret_cast<const void*>(&HullBuild));
        Jump(image+0x1C8E380,reinterpret_cast<const void*>(&HullConfigDestroy));
        FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
        alignas(16) float points[4][4]{{1,0,0,0},{2,0,0,0},{3,0,0,0},{4,0,0,0}};
        struct Array {void* data;int count;uint32_t capacity;} array{points,4,0x80000004};
        auto build=reinterpret_cast<void*(*)(Array*,unsigned,bool)>(image+0x161CBF0);
        Check(build(&array,252,false)==reinterpret_cast<void*>(0x12345678)&&hullBuildCalls==1,"exact native convex no-cache path returns shape");
        puts("PASS: exact Replay convex wrapper consumes aligned hkArray and forwards verified no-cache hull parameters (Havok builder mocked)");
    }
    compoundcollision::Array* expectedCompound=nullptr;
    unsigned compoundBuildCalls=0,compoundAllocCalls=0;
    alignas(16) unsigned char compoundStorage[288]{};
    uintptr_t compoundHeap[2]{},compoundRouter[12]{};
    void* CompoundTls(unsigned long){return compoundRouter;}
    void* CompoundAlloc(void*,int bytes){Check(bytes==288,"native compound allocator size");++compoundAllocCalls;return compoundStorage;}
    void* CompoundBuild(void* memory,void* config){
        auto* array=static_cast<compoundcollision::Array*>(config);
        Check(memory==compoundStorage && array->data==expectedCompound->data && array->size==expectedCompound->size,"native compound forwards exact instance pointer/count");
        Check(reinterpret_cast<uintptr_t>(array->data)%16==0,"compound instance alignment");
        for(int i=0;i<array->size;++i){
            auto& instance=array->data[i];uint32_t flags=0;memcpy(&flags,&instance.transform[3],4);
            Check(instance.shape==reinterpret_cast<void*>(size_t(i+1)) && flags==0x3F000040 && instance.transform[15]==1 && instance.scale[3]==1 && instance.shapeTag==0xFFFF && instance.instanceId==0xFFFF,"native identity flags, references and metadata");
        }
        ++compoundBuildCalls;return memory;
    }
    void CompoundNativeTests(){
        NativeBytes(0x161C770,ReplayCompoundCode);
        for(uintptr_t rva:{0x1E459F0,0x1E45620,0x23513B8,0x12D42A40})Commit(rva);
        NativeBytes(0x1E459F0,ReplayCompoundInfoCode);
        Jump(image+0x1E45620,reinterpret_cast<const void*>(&CompoundBuild));
        auto tls=&CompoundTls;memcpy(image+0x23513B8,&tls,8);
        compoundHeap[1]=reinterpret_cast<uintptr_t>(&CompoundAlloc);
        uintptr_t heap=reinterpret_cast<uintptr_t>(compoundHeap);compoundRouter[11]=reinterpret_cast<uintptr_t>(&heap);
        FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
        std::vector<compoundcollision::Instance> instances(256);
        for(size_t i=0;i<instances.size();++i)instances[i].shape=reinterpret_cast<void*>(i+1);
        compoundcollision::Array array{instances.data(),256,0x80000100};expectedCompound=&array;
        auto build=reinterpret_cast<void*(*)(compoundcollision::Array*)>(image+0x161C770);
        Check(build(&array)==compoundStorage && compoundBuildCalls==1 && compoundAllocCalls==1,"exact native compound wrapper constructs a batch");
        array.size=0;Check(build(&array)==nullptr && compoundBuildCalls==1,"empty compound does not allocate");
        Check(compoundcollision::BodyCount(325)==325 && compoundcollision::BodyCount(16965)==67 && compoundcollision::BodyCount(10740)==42,"both owner crash maps fit small native body counts without dropping hulls");
        puts("PASS: exact Replay compound wrapper and aligned 112-byte instances; Office 16965 hulls -> 67 bodies, Nuketown 10740 -> 42 (allocator/constructor mocked)");
    }
    unsigned ladderFallbacks=0,ladderTraces=0;
    void LadderIdentityAxis(const float*,float* out){memset(out,0,36);out[0]=out[4]=out[8]=1;}
    void LadderIdentityQuat(const float*,const float* in,float* out){memcpy(out,in,12);}
    float LadderFmod(float a,float b){return std::fmod(a,b);}
    void LadderCookie(uintptr_t){}
    void LadderHandTargetTests(){
        ladderikfixture::Copy([](uintptr_t rva,const auto& bytes){NativeBytes(rva,bytes);});
        for(auto rva:{0x20375E0,0x203B3C0,0x2213200,0x21CCF60})Commit(rva);
        Jump(image+0x20375E0,reinterpret_cast<void*>(&LadderIdentityAxis));Jump(image+0x203B3C0,reinterpret_cast<void*>(&LadderIdentityQuat));
        Jump(image+0x2213200,reinterpret_cast<void*>(&LadderFmod));Jump(image+0x21CCF60,reinterpret_cast<void*>(&LadderCookie));
        using Target=void(*)(const float*,const float*,float*,int,void*);auto target=reinterpret_cast<Target>(image+0x14A94E0);
        const float quat[]{0,0,0,1},rung=25.978f;float hand[]{0,0,73.978f},out[3]{};
        std::array<unsigned char,0x100> node{};
        auto put=[&](size_t offset,float value){memcpy(node.data()+offset,&value,4);};
        put(0x14,rung);put(0x20,rung+144);put(0x34,24);put(0x88,-1);
        target(hand,quat,out,0,node.data());const float wristZ=*reinterpret_cast<float*>(image+0x24CD848);
        const float phase=std::remainder(out[2]-wristZ-rung,24.f);
        Check(std::abs(phase)<.001f,"actual Replay IK snaps hand contact to the measured rung grid");
        put(0x14,rung+12);put(0x20,rung+156);put(0x88,-1);target(hand,quat,out,0,node.data());
        Check(std::abs(std::abs(std::remainder(out[2]-wristZ-rung,24.f))-12)<.001f,"actual Replay IK reproduces v20 half-rung hand miss");
        puts("PASS: actual Replay ladder hand-target machine code reproduces v20 12-unit miss and validates corrected rung phase (identity transforms and CRT helpers supplied)");
    }
    unsigned long long observedLadderButtons=0;
    void LadderCheck(void* pm,void*){memcpy(&observedLadderButtons,static_cast<char*>(pm)+0x10,8);}
    struct LadderInfo {float axis[9],bottom[3],top[3],width,rung;};
    bool LadderFallback(const float*,const void*,LadderInfo*,float*,bool,unsigned*,unsigned*) {++ladderFallbacks;return false;}
    void LadderTrace(void*,void*,void* result,const float*,const float*,const float*,int pass,int mask,bool cheap) {
        Check(pass==7 && mask==0x10001 && cheap,"ladder trace preserves all nine arguments");
        memset(result,0,64);const float fraction=.25f;memcpy(result,&fraction,4);++ladderTraces;
    }
    unsigned movementTraceCalls=0;
    bool movementMiss=false;
    void MovementTraceStock(void* handler,void* pm,void* result,const float* start,const float* end,const float* bounds,int pass,int mask,int flags,bool cheap){
        Check(handler==reinterpret_cast<void*>(1)&&pm==reinterpret_cast<void*>(2)&&start[2]==2&&end[2]==-2&&bounds[5]==36&&pass==7&&mask==0x10001&&flags==32&&cheap,"modern ground trace preserves all ten native arguments");
        ++movementTraceCalls;memset(result,0,72);float fraction=movementMiss?1.f:.5f,normal=1;unsigned type=1;unsigned short entity=2046;
        memcpy(result,&fraction,4);memcpy(static_cast<char*>(result)+12,&normal,4);memcpy(static_cast<char*>(result)+0x24,&type,4);memcpy(static_cast<char*>(result)+0x2C,&entity,2);
    }
    void MovementSurfaceTests(){
        Commit(replay::MovementTrace.rva);
        PrologueFixture(replay::MovementTrace,{0x41,0x5F,0x41,0x5E,0x41,0x5C,0x5F,0x5E,0x5D},reinterpret_cast<void*>(&MovementTraceStock));
        Check(customsurfaces::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"modern movement trace installs at verified Replay wrapper");
        auto authored=std::make_shared<customsurfaces::Data>();authored->triangles.push_back({{{0,0,0},{0,64,0},{64,0,0}},3});authored->cells[customsurfaces::Key(0,0)].push_back(0);customsurfaces::data.store(authored);
        using Trace=void(*)(void*,void*,void*,const float*,const float*,const float*,int,int,int,bool);
        auto trace=reinterpret_cast<Trace>(image+replay::MovementTrace.rva);std::array<unsigned char,72> result{};float start[]{16,16,2},end[]{16,16,-2},bounds[]{0,0,36,15,15,36};
        trace(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),result.data(),start,end,bounds,7,0x10001,32,true);
        unsigned flags=0;memcpy(&flags,result.data()+0x1C,4);Check(flags==(3u<<19)&&movementTraceCalls==1,"flat carpet tagged through modern path without calling legacy trace");
        movementMiss=true;trace(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),result.data(),start,end,bounds,7,0x10001,32,true);memcpy(&flags,result.data()+0x1C,4);Check(flags==0,"modern airborne trace does not invent a floor");movementMiss=false;
        customsurfaces::Clear();trace(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),result.data(),start,end,bounds,7,0x10001,32,true);memcpy(&flags,result.data()+0x1C,4);Check(flags==(5u<<19),"modern floor with missing metadata receives concrete fallback");
        puts("PASS: modern movement trace ABI, flat carpet tagging independent of legacy hooks, airborne preservation and concrete fallback");
    }
    void LadderTests() {
        Check(customphysics::OwnsEmptyWorld(),"ladder fixture owns selected custom collision world");
        {
            std::array<unsigned char,72> ground{};float fraction=.5f,normal=1;unsigned type=1,flags=8;unsigned short world=2046;
            memcpy(ground.data(),&fraction,4);memcpy(ground.data()+12,&normal,4);memcpy(ground.data()+0x24,&type,4);memcpy(ground.data()+0x2C,&world,2);memcpy(ground.data()+0x1C,&flags,4);
            const float start[]{0,0,2},end[]{0,0,-2};customsurfaces::Clear();customsurfaces::Apply(ground.data(),start,end);
            memcpy(&flags,ground.data()+0x1C,4);Check(flags==(5u<<19|8u),"untyped custom world trace receives concrete footstep category and preserves unrelated flags");
            auto authored=std::make_shared<customsurfaces::Data>();authored->triangles.push_back({{{0,0,0},{0,64,0},{64,0,0}},21});authored->cells[customsurfaces::Key(0,0)].push_back(0);customsurfaces::data.store(authored);
            customsurfaces::Apply(ground.data(),start,end);memcpy(&flags,ground.data()+0x1C,4);Check(flags==(21u<<19|8u),"generic PM_Concrete body is refined to authored wood without losing ladder flags");customsurfaces::Clear();
            flags=21u<<19|8u;memcpy(ground.data()+0x1C,&flags,4);const auto typed=ground;customsurfaces::Apply(ground.data(),start,end);Check(ground==typed,"native typed ground remains unchanged");
            flags=0;world=7;memcpy(ground.data()+0x1C,&flags,4);memcpy(ground.data()+0x2C,&world,2);const auto entity=ground;customsurfaces::Apply(ground.data(),start,end);Check(ground==entity,"entity collision does not get world footstep overrides");
        }

        const auto dir=packageRoot/id;
        {std::ofstream f(dir/"ladders.bin",std::ios::binary);f.write("MWRLAD01",8);const unsigned n=1;f.write(reinterpret_cast<const char*>(&n),4);
         const ladderfile::Face face{{0,0,0},{0,0,120},{1,0,0},32};f.write(reinterpret_cast<const char*>(&face),40);}
        customladders::Load(dir);
        Commit(replay::GetLadderInfo.rva);Commit(replay::LegacyPlayerTrace.rva);
        Commit(replay::CheckLadderMove.rva);
        PrologueFixture(replay::CheckLadderMove,{0x41,0x5D,0x5F,0x5E,0x5D},reinterpret_cast<void*>(&LadderCheck));
        PrologueFixture(replay::GetLadderInfo,{0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5B,0x5D},reinterpret_cast<void*>(&LadderFallback));
        PrologueFixture(replay::LegacyPlayerTrace,{0x48,0x81,0xC4,0x90,0,0,0,0x5F},reinterpret_cast<void*>(&LadderTrace));
        Check(customladders::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"both ladder hooks install against exact prologues");
        auto get=reinterpret_cast<bool(*)(const float*,const void*,LadderInfo*,float*,bool,unsigned*,unsigned*)>(image+replay::GetLadderInfo.rva);
        float origin[]{20,5,20},center[3]{};LadderInfo info{};
        std::array<unsigned char,128> pm{},ps{};auto* psPtr=ps.data();memcpy(pm.data()+8,&psPtr,8);memcpy(ps.data()+0x30,origin,12);
        unsigned long long buttons=4;memcpy(pm.data()+0x10,&buttons,8);
        auto check=reinterpret_cast<void(*)(void*,void*)>(image+replay::CheckLadderMove.rva);check(pm.data(),nullptr);
        Check(observedLadderButtons==(buttons|0x0800000000000000ull) && *reinterpret_cast<unsigned long long*>(pm.data()+0x10)==buttons,"authored ladder arms native edge-input gate and restores original command");
        float distant=100;memcpy(ps.data()+0x34,&distant,4);check(pm.data(),nullptr);Check(observedLadderButtons==buttons,"distant movement input is unchanged");
        Check(get(origin,nullptr,&info,center,false,nullptr,nullptr) && info.axis[0]==1 && info.axis[4]==1 && info.axis[8]==1 && info.top[2]==120 && info.rung==12 && center[1]==-5,"native ladder-info layout and lateral centering");
        origin[1]=100;Check(!get(origin,nullptr,&info,center,false,nullptr,nullptr)&&ladderFallbacks==1,"distant ladder queries preserve stock fallback");
        // Forward the nine-argument ABI from a wrapper whose CALL ends at the
        // exact first PM_CheckLadderMove return address consumed by the hook.
        std::vector<unsigned char> prefix{0x48,0x83,0xEC,0x58};
        for(unsigned i=0;i<5;++i) {
            const unsigned char load[]{0x48,0x8B,0x84,0x24,static_cast<unsigned char>(0x80+i*8),0,0,0};prefix.insert(prefix.end(),load,load+8);
            const unsigned char save[]{0x48,0x89,0x44,0x24,static_cast<unsigned char>(0x20+i*8)};prefix.insert(prefix.end(),save,save+5);
        }
        const uintptr_t call=0xCC63B1,entry=call-prefix.size();Commit(entry);
        memcpy(image+entry,prefix.data(),prefix.size());image[call]=0xE8;
        const int displacement=static_cast<int>(replay::LegacyPlayerTrace.rva-call-5);memcpy(image+call+1,&displacement,4);
        const unsigned char after[]{0x48,0x83,0xC4,0x58,0xC3};memcpy(image+call+5,after,5);
        FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
        using Trace=void(*)(void*,void*,void*,const float*,const float*,const float*,int,int,bool);
        auto trace=reinterpret_cast<Trace>(image+entry),direct=reinterpret_cast<Trace>(image+replay::LegacyPlayerTrace.rva);
        std::array<unsigned char,64> result{};float start[]{20,0,0},end[]{0,0,0},bounds[]{0,0,36,15,15,36};
        trace(nullptr,nullptr,result.data(),start,end,bounds,7,0x10001,true);Check((result[0x1C]&8)!=0,"native ladder caller marks authored capsule contact climbable");
        direct(nullptr,nullptr,result.data(),start,end,bounds,7,0x10001,true);Check(result[0x1C]==0,"ordinary trace at ladder remains unmodified");
        start[1]=end[1]=100;trace(nullptr,nullptr,result.data(),start,end,bounds,7,0x10001,true);Check(result[0x1C]==0 && ladderTraces==3,"nearby wall outside authored ladder width remains unclimbable");
        {std::ofstream f(dir/"ladders.bin",std::ios::binary);f.write("MWRLAD02",8);unsigned count=1;f.write(reinterpret_cast<char*>(&count),4);
         ladderfile::Face face{{0,0,0},{0,0,120},{1,0,0},32,{2,0,12},24,23};f.write(reinterpret_cast<char*>(&face),sizeof(face));}
        customladders::Load(dir);origin[1]=5;
        Check(get(origin,nullptr,&info,center,false,nullptr,nullptr) && info.rung==24 && info.width==23 && info.bottom[0]==2 && info.bottom[2]==12 && info.top[2]==132,"measured rung phase, grip plane and width reach native ladder animation data");
        fs::remove(dir/"ladders.bin");customladders::Load(dir);
        LadderHandTargetTests();
        puts("PASS: ladder native prologues, seven/nine-argument ABI, ladder-info layout, authored capsule contact, ordinary-trace and nearby-wall negative cases (trace collision mocked)");
    }
    #include "replay_glass_tests.h"
    #include "custom_audio_tests.h"
    #include "shared_collision_tests.h"
    void PhysicsTests()
    {
        using namespace physicsfixture;
        NativeBytes(AddShapeListRva,AddShapeList);NativeBytes(AddMapEntsRva,AddMapEnts);NativeBytes(AddClipMapRva,AddClipMap);
        NativeBytes(SetMainShapeListRva,SetMainShapeList);NativeBytes(PhysicsAddThunkRva,PhysicsAddThunk);NativeBytes(PhysicsMainThunkRva,PhysicsMainThunk);
        for(uintptr_t rva:{0xE5C42E0,0xE5C8318,0xE5C62F0,0x2351428,0xF042CF0,0xF0465E8})Commit(rva);
        auto enter=&PhysicsEnter,leave=&PhysicsLeave;
        memcpy(image+0x2351428,&enter,8);memcpy(image+0x2351418,&leave,8);
        for(uintptr_t rva:{0x1C9B480,0x1C9CF00,0x1C95660,0x1C9B6D0,0x1640720,0x1640730})Commit(rva);
        Jump(image+0x1C9B480,reinterpret_cast<const void*>(&BeginReflection));
        Jump(image+0x1C9CF00,reinterpret_cast<const void*>(&Deserialize));
        Jump(image+0x1C95660,reinterpret_cast<const void*>(&TypeMatches));
        Jump(image+0x1C9B6D0,reinterpret_cast<const void*>(&EndReflection));
        Jump(image+0x1640720,reinterpret_cast<const void*>(&ClearMain));
        Jump(image+0x1640730,reinterpret_cast<const void*>(&SetMain));
        Check(customphysics::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"checked custom physics hook installed");
        Check(customphysics::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"physics installation idempotent");
        FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
        Check(custommaps::Select(id.c_str()),"select physics fixture package");
        std::array<unsigned char,0x428> mapEnts{};std::array<unsigned char,0xF8> clipMap{};
        const std::string name="maps/mp/"+id+".d3dbsp";const char* namePtr=name.c_str();
        memcpy(mapEnts.data(),&namePtr,8);memcpy(clipMap.data(),&namePtr,8);
        auto addMap=reinterpret_cast<void(*)(void*)>(image+AddMapEntsRva);
        auto addClip=reinterpret_cast<void(*)(void*)>(image+AddClipMapRva);
        addMap(mapEnts.data());
        Check(deserializeCalls==0&&*reinterpret_cast<void**>(image+0xE5C42E0)==mapEnts.data()&&*reinterpret_cast<void**>(image+0xE5C8318)==nullptr,"empty selected MapEnts retains native registration with no shapes");
        addClip(clipMap.data());
        Check(deserializeCalls==0&&clearMainCalls==1&&physicsLocks==0,"empty selected clipMap uses native nullable SetMainShapeList branch");
        LadderTests();
        MovementSurfaceTests();
        AudioTests();
        GlassTests();
        SharedCollisionTests();
        unsigned dataSize=4;char data[4]{};char* dataPtr=data;
        auto nonemptyMap=mapEnts;
        memcpy(nonemptyMap.data()+0x148,&dataSize,4);memcpy(nonemptyMap.data()+0x150,&dataPtr,8);
        addMap(nonemptyMap.data());
        Check(deserializeCalls==1&&lastRaw==data&&lastRawSize==4&&setMainCalls==1,"nonempty custom physics uses original deserializer and main-shape registration");
        memset(mapEnts.data()+0x148,0,16);mapEnts[0x158]=1;
        addMap(mapEnts.data());Check(deserializeCalls==2,"nonempty submodel count prevents empty-prototype handling");mapEnts[0x158]=0;
        auto direct=reinterpret_cast<void*(*)(char**,unsigned*,const char*,int)>(image+AddShapeListRva);
        Check(direct(reinterpret_cast<char**>(mapEnts.data()+0x150),reinterpret_cast<unsigned*>(mapEnts.data()+0x148),namePtr,29)==shapeList.data()&&deserializeCalls==3,"unrecognized caller keeps original deserialization even for zero data");
        custommaps::ClearSelection();addMap(mapEnts.data());addClip(clipMap.data());
        Check(deserializeCalls==5,"unselected map and clip readers keep original behavior");
        const char* stock="maps/mp/mp_shipment.d3dbsp";memcpy(mapEnts.data(),&stock,8);
        Check(custommaps::Select(id.c_str()),"reselect custom fixture");addMap(mapEnts.data());
        Check(deserializeCalls==6,"stock map remains original while a custom package is selected");
        custommaps::ClearSelection();
        puts("PASS: exact Replay physics callers, empty custom registration and nullable main-shape branch; nonempty, submodel, stock, unselected and unrelated callers remain native (reflection mocked)");
    }
    unsigned omnvarLoads=0, omnvarOverflows=0;
    unsigned& DefinitionCount(){return *reinterpret_cast<unsigned*>(image+0x5C48518);}
    unsigned& ArchivedCount(){return *reinterpret_cast<unsigned*>(image+0x5C4852C);}
    const char* forwardedOmnvarMap=nullptr;
    uintptr_t NativeOmnvarLoad(unsigned char mode,const char* type,const char* map) {
        Check(mode==2 && type!=nullptr,"omnvar native arguments survive");
        forwardedOmnvarMap=map;++omnvarLoads;
        // Simulate the captured frontend append against retained match definitions.
        if(std::strcmp(map,"mp_frontend3")==0 && ArchivedCount()>=78)++omnvarOverflows;
        DefinitionCount()+=449;ArchivedCount()+=78;
        SetLastError(0x9876);return 0x1234;
    }
    void OmnvarTests() {
        for(uintptr_t rva=0x5C48000;rva<0x5C60800;rva+=0x1000)Commit(rva);
        Commit(0xCD5C80);Commit(0x137B1B0);Commit(0x21F6CC0);
        NativeBytes(metadatafixture::ClearOmnvarsRva,metadatafixture::ClearOmnvars);
        Jump(image+0x21F6CC0,reinterpret_cast<const void*>(&std::memset));
        // Undo the real LoadTables prologue before a controlled table-reader callback.
        PrologueFixture(replay::OmnvarLoad,{0x48,0x81,0xC4,0x40,0x04,0,0,0x5F},reinterpret_cast<const void*>(&NativeOmnvarLoad));
        memcpy(image+replay::FrontendOmnvarCall.rva,replay::FrontendOmnvarCall.bytes,replay::FrontendOmnvarCall.size);
        Check(customomnvars::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"checked omnvar lifecycle hooks installed");
        Check(customomnvars::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"omnvar installation idempotent");
        // A fixture caller with the captured return address. Keep the five-byte native call.
        const unsigned char before[]={0x48,0x83,0xEC,0x28};
        memcpy(image+0x137B1C5,before,sizeof(before));
        const unsigned char after[]={0x48,0x83,0xC4,0x28,0xC3};memcpy(image+0x137B1CE,after,sizeof(after));
        using Loader=uintptr_t(*)(unsigned char,const char*,const char*);
        auto load=reinterpret_cast<Loader>(image+replay::OmnvarLoad.rva);
        auto frontendLoad=reinterpret_cast<Loader>(image+0x137B1C5);
        auto clear=reinterpret_cast<uintptr_t(*)()>(image+replay::OmnvarClear.rva);
        FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
        clear();Check(custommaps::Select(id.c_str()),"select custom omnvar fixture");
        load(2,"war",id.c_str());Check(ArchivedCount()==78,"custom match produces captured archived count");
        const char* front="mp_frontend3";
        Check(frontendLoad(2,"war",front)==0x1234 && GetLastError()==0x9876 && forwardedOmnvarMap==front,"recovery preserves original return, LastError and input pointer");
        Check(ArchivedCount()==78 && omnvarOverflows==0,"frontend recovery clears exact native state before one existing load");
        clear();load(2,"war",id.c_str());load(2,"war",front);
        Check(omnvarOverflows==1 && ArchivedCount()==156,"unrelated caller is not reset");
        clear();load(2,"war",id.c_str());clear();
        Check(DefinitionCount()==0 && ArchivedCount()==0 && *reinterpret_cast<uintptr_t*>(image+0x5C48510)==reinterpret_cast<uintptr_t>(image+0x5C36D10),"native reset clears counts and restores string-buffer tail");
        frontendLoad(2,"war",front);Check(ArchivedCount()==78,"normal native clear permits one frontend load");
        clear();load(2,"war","mp_shipment");frontendLoad(2,"war",front);
        Check(omnvarOverflows==2,"stock table ownership does not arm custom recovery");
        clear();load(2,"war",id.c_str());custommaps::ClearSelection();frontendLoad(2,"war",front);
        Check(omnvarOverflows==3,"unselected package does not reset tables");clear();
        puts("PASS: captured frontend recovery uses exact native Omnvar reset; single load, native ABI, normal clear, stock and unrelated callers preserved");
    }
    struct NcsAsset {const char* name;unsigned type,source,flags,count;const char** strings;};
    std::vector<NcsAsset> ncsAssets;
    unsigned missingNcs=0,findNcsCalls=0;
    uintptr_t NcsFind(int type,const char* name,int allowDefault) {
        Check(type==61 && allowDefault==0,"native NCS gate requests exact asset type without defaults");++findNcsCalls;
        for(auto& asset:ncsAssets)if(!strcmp(asset.name,name))return reinterpret_cast<uintptr_t>(&asset);
        return 0;
    }
    void MissingNcs(int,const char*,const char*){++missingNcs;}
    int NcsFormat(char* dest,size_t size,const char* format,...) {
        va_list args;va_start(args,format);int result=vsnprintf(dest,size,format,args);va_end(args);return result;
    }
    void NcsNoop(){}
    int NcsZero(){return 0;}
    void NetConstTests(const char* package) {
        NativeBytes(metadatafixture::BuildStringMapRva,metadatafixture::BuildStringMap);
        for(uintptr_t rva:{0x5A543F8,0xC6E0000,0x2436000,0x4598000,0x23EF000,0x10F0B90,0x10C8980,0x1294C60,0x12B0820,0x20365D0,0x12AB4A0,0x21CCF60})Commit(rva);
        for(uintptr_t rva:{0x10F0B90,0x12B0820,0x21CCF60})Jump(image+rva,reinterpret_cast<const void*>(&NcsNoop));
        for(uintptr_t rva:{0x10C8980,0x1294C60})Jump(image+rva,reinterpret_cast<const void*>(&NcsZero));
        Jump(image+0x20365D0,reinterpret_cast<const void*>(&NcsFormat));
        Jump(image+replay::FindAsset.rva,reinterpret_cast<const void*>(&NcsFind));
        Jump(image+0x12AB4A0,reinterpret_cast<const void*>(&MissingNcs));
        strcpy_s(reinterpret_cast<char*>(image+0x23EFEEC),6,"level");
        strcpy_s(reinterpret_cast<char*>(image+0x2436590),16,"ncs_%s_%s");
        for(unsigned i=0;i<replayncs::Count;++i)memcpy(image+0x4598378+i*24,&replayncs::Tags[i],8);
        // Stop after the exact required-assets loop; omit unrelated global map building.
        const int32_t delta=0x10EFDCB-(0x10EFCDC+5);image[0x10EFCDC]=0xE9;memcpy(image+0x10EFCDD,&delta,4);
        FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
        auto gate=reinterpret_cast<void(*)()>(image+metadatafixture::BuildStringMapRva);
        gate();Check(missingNcs==41 && findNcsCalls==41,"v8 omission reproduces all missing level assets through native gate");
        std::vector<std::string> names;names.reserve(41);ncsAssets.reserve(41);
        if (package && *package) {
        fs::path path;
        for(const auto& entry:fs::directory_iterator(package))
            if(entry.path().filename().string().starts_with("srv_") && entry.path().extension()==".ff") {
                Check(path.empty(),"one server fastfile per package");path=entry.path();
            }
        std::ifstream file(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
        Check(bytes.size()>41*46,"read v9 server package");
        size_t pos=bytes.size()-41*46;
        for(unsigned i=0;i<41;++i) {
            NcsAsset asset{};memcpy(&asset,bytes.data()+pos,32);pos+=32;
            names.emplace_back(bytes.data()+pos,13);pos+=14;asset.name=names.back().c_str();
            Check(asset.type==i && asset.source==2 && asset.flags==0 && asset.count==0 && asset.strings==nullptr,"serialized NCS type/source/count layout");
            ncsAssets.push_back(asset);
        }
        } else {
            for(unsigned i=0;i<replayncs::Count;++i) {
                names.emplace_back(std::string("ncs_")+replayncs::Tags[i]+"_level");
                NcsAsset asset{};asset.name=names.back().c_str();asset.type=i;asset.source=2;
                ncsAssets.push_back(asset);
            }
        }
        missingNcs=findNcsCalls=0;gate();Check(missingNcs==0 && findNcsCalls==41,"all supplied NCS assets satisfy exact native required-assets loop");
        auto removed=ncsAssets.front();ncsAssets.erase(ncsAssets.begin());missingNcs=0;gate();
        Check(missingNcs==1,"removing model level metadata restores missing-asset failure");
        puts("PASS: exact Replay NCS presence loop rejects v8 omission and accepts all 41 level metadata fixtures; missing-model negative case verified");
    }

}

void NativeMapList() {
    auto* rows=image+0xC4FD8B8;
    memset(rows,0,0x4D8*2);
    memcpy(rows+0x20,"mp_shipment",12);
    memcpy(rows+0x50,"shipment_preview",17);
    memcpy(rows+0x4D8+0x20,"mp_stock",9);
    *reinterpret_cast<int*>(image+0xC4FD8B4)=2;
}
void MapMenuTests() {
    const std::string menuId="mp_ui"+std::to_string(GetCurrentProcessId());
    Check(menuId.size()<16,"native map id fixture fits engine field");
    MakePackage(menuId);custommaps::Refresh();
    Commit(replay::LoadMapInfoList.rva);
    for(uintptr_t page=0xC4FD000;page<0xC4FD8B8+128*0x4D8;page+=0x1000)Commit(page);
    PrologueFixture(replay::LoadMapInfoList,{0x48,0x83,0xC4,0x50,0x41,0x5D},reinterpret_cast<void*>(&NativeMapList));
    Check(custommapui::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"native map-list hook installs at exact prologue");
    auto load=reinterpret_cast<void(*)()>(image+replay::LoadMapInfoList.rva);
    load();
    auto* row=image+0xC4FD8B8+2*0x4D8;
    Check(*reinterpret_cast<int*>(image+0xC4FD8B4)==3,"only valid native-length custom package is appended");
    Check(std::string(reinterpret_cast<char*>(row+0x20))==menuId && row[0]==31,"custom map id and literal display label registered");
    Check(std::string(reinterpret_cast<char*>(row+0x50))=="mw120r/"+menuId &&
        std::string(reinterpret_cast<char*>(row+0x70))=="mw120r/"+menuId,"native map and vote images use this custom package");
    Check(std::string(reinterpret_cast<char*>(row+0xD0))=="war","TDM map filter registered");
    load();Check(*reinterpret_cast<int*>(image+0xC4FD8B4)==3,"native map-list reload does not duplicate entries");
    custommapui::SyncSelection(menuId.c_str());Check(custommaps::Active()==menuId,"normal map choice activates package routing");
    custommapui::SyncSelection("mp_shipment");Check(custommaps::Active().empty(),"stock map choice clears custom routing");
    fs::remove_all(packageRoot/menuId);custommaps::Refresh();
    puts("PASS: native map-list hook, display fields, valid package filtering, reload and normal-UI selection routing (arena loader mocked)");
}
void CollisionFileTests() {
    std::vector<uint8_t> data(36);memcpy(data.data(),"MWCOLL01",8);
    uint32_t count=1;memcpy(data.data()+8,&count,4);
    float bounds[]{-24,-32,0,24,32,64};memcpy(data.data()+12,bounds,24);
    std::vector<collisionfile::Brush> brushes;
    Check(collisionfile::Parse(data,brushes)&&brushes.size()==1&&brushes[0].maxs[2]==64,"portable authored collision bounds");
    auto truncated=data;truncated.pop_back();Check(!collisionfile::Parse(truncated,brushes)&&brushes.empty(),"truncated collision clears output");
    auto invalid=data;invalid[0]='X';Check(!collisionfile::Parse(invalid,brushes),"invalid collision magic rejected");
    count=4097;auto large=data;large.resize(12+count*24);
    memcpy(large.data()+8,&count,4);for(uint32_t i=0;i<count;++i)memcpy(large.data()+12+i*24,bounds,24);
    Check(collisionfile::Parse(large,brushes)&&brushes.size()==count,"compiled map collision exceeds old 4096 limit");
    invalid=data;count=32769;memcpy(invalid.data()+8,&count,4);Check(!collisionfile::Parse(invalid,brushes),"excess collision count rejected");
    invalid=data;float nan=std::numeric_limits<float>::quiet_NaN();memcpy(invalid.data()+12,&nan,4);
    Check(!collisionfile::Parse(invalid,brushes),"nonfinite collision rejected");
    invalid=data;memcpy(invalid.data()+24,bounds,4);Check(!collisionfile::Parse(invalid,brushes),"degenerate collision rejected");
    invalid=data;invalid.push_back(0);Check(!collisionfile::Parse(invalid,brushes),"trailing collision data rejected");
    puts("PASS: authored collision file validates bounds, counts, truncation and nonfinite data");
}

#include "replay_noclip_tests.h"
#include "custom_image_tests.h"
#include "custom_ambient_tests.h"
#include "custom_surface_tests.h"

void RunFixture(void(*test)(),const char* name) {
    // Check a nonvolatile register around native-code fixtures independently
    // of the compiler's changing register allocation in the test runner.
    static auto* thunk=[] {
        auto* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        const unsigned char code[]{0x41,0x57,0x48,0x83,0xEC,0x20,0x49,0xBF,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0xFF,0xD1,0x4C,0x89,0xF8,0x48,0x83,0xC4,0x20,0x41,0x5F,0xC3};
        memcpy(p,code,sizeof(code));return p;
    }();
    auto value=reinterpret_cast<uintptr_t(*)(void(*)())>(thunk)(test);
    if(value!=0x5555555555555555ull){fprintf(stderr,"FAIL: %s clobbered nonvolatile R15: %llX\n",name,value);exit(1);}
}

int main(int argc,char** argv)
{
    setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* e)->LONG {
        if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
            fprintf(stderr,"Fixture AV: pc=%p image=%p test_exe_rva=%llX access=%llu address=%p rdi=%llX rbx=%llX rcx=%llX rdx=%llX\n",
                reinterpret_cast<void*>(e->ContextRecord->Rip),image,e->ContextRecord->Rip-reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                e->ExceptionRecord->ExceptionInformation[0],reinterpret_cast<void*>(e->ExceptionRecord->ExceptionInformation[1]),
                e->ContextRecord->Rdi,e->ContextRecord->Rbx,e->ContextRecord->Rcx,e->ContextRecord->Rdx);
        return EXCEPTION_CONTINUE_SEARCH;
    });
    log120r::Init(GetModuleHandleW(nullptr),false);
    for(auto test:std::vector<std::pair<void(*)(),const char*>>{{CollisionFileTests,"collision"},{ParserTests,"parser"},{PackageTests,"packages"},{RuntimeTests,"UI"},{NoclipTests,"noclip"},{MapMenuTests,"map menu"},{RenderTests,"render"},{PhysicsTests,"physics/ladder/glass"},{ConvexNativeTests,"convex"},{CompoundNativeTests,"compound"},{OmnvarTests,"omnvars"}})RunFixture(test.first,test.second);
    NetConstTests(argc>1 ? argv[1] : nullptr);
    RunFixture(CustomImageTests,"custom images");
    RunFixture(CustomAmbientTests,"custom ambient");
    RunFixture(CustomSurfaceTests,"custom surfaces");
    for(int packageIndex=1;packageIndex<argc;++packageIndex) if(fs::exists(fs::path(argv[packageIndex])/"collision.bin")) {
        if(fs::exists(fs::path(argv[packageIndex])/"footsteps.bin")) {
            const auto dir=fs::path(argv[packageIndex]);std::ifstream f(dir/"footsteps.bin",std::ios::binary);unsigned count=0;f.seekg(8);f.read((char*)&count,4);
            customsurfaces::Load(dir);auto surfaceData=customsurfaces::data.load();Check(surfaceData&&surfaceData->triangles.size()==count,"actual authored footstep sidecar and spatial index accepted");
            printf("PASS: actual authored footstep data: %u triangles (%s)\n",count,argv[packageIndex]);customsurfaces::Clear();
        }
        std::vector<collisionfile::Brush> brushes;
        Check(collisionfile::Load(fs::path(argv[packageIndex])/"collision.bin",brushes)&&!brushes.empty(),"actual authored package collision accepted");
        printf("PASS: actual authored package collision: %zu shapes\n",brushes.size());
        if(fs::exists(fs::path(argv[packageIndex])/"glass.bin")) {
            std::vector<glassfile::Pane> panes;unsigned surfaces=0;
            Check(glassfile::Load(fs::path(argv[packageIndex])/"glass.bin",panes,surfaces),"actual authored glass passes native float32 geometry validation");
            printf("PASS: actual authored glass: %zu panes, %u surfaces (%s)\n",panes.size(),surfaces,argv[packageIndex]);
        }
    }
    // Only the three process-specific directories created by this test are removed.
    for(const auto& name:{id,std::string(id+"_nested"),std::string(id+"_large")})
        fs::remove_all(packageRoot/name);
    return 0;
}
