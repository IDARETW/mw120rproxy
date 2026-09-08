#include "engine_diagnostics.h"
#include "engine_console_gate.h"
#include "replay_bindings.h"
#include "replay_decoder_fixture.h"
#include "log.h"
#include "asset_context.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs=std::filesystem;
namespace {
void Check(bool value,const char* message){if(!value){fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
unsigned char* image;
void Commit(uintptr_t rva,size_t size=0x1000){const auto start=rva&~uintptr_t{0xFFF};Check(VirtualAlloc(image+start,((rva+size-start+0xFFF)&~size_t{0xFFF}),MEM_COMMIT,PAGE_EXECUTE_READWRITE)!=nullptr,"commit fixture");}
void Jump(uintptr_t rva,const void* target){Commit(rva);const unsigned char op[]{0xFF,0x25,0,0,0,0};memcpy(image+rva,op,6);memcpy(image+rva+6,&target,8);}
template<size_t N> void Copy(uintptr_t rva,const unsigned char(&data)[N]){Commit(rva,N);memcpy(image+rva,data,N);}
template<class T> void Put(std::vector<unsigned char>& buffer,size_t at,T value){memcpy(buffer.data()+at,&value,sizeof(value));}
template<class T> T Get(const std::vector<unsigned char>& buffer,size_t at){T value;memcpy(&value,buffer.data()+at,sizeof(value));return value;}
void* Memcpy(void* to,const void* from,size_t size){return memcpy(to,from,size);}
void* Memset(void* to,int ch,size_t size){return memset(to,ch,size);}
unsigned invalidFrames=0;
void InvalidFrame(uintptr_t){++invalidFrames;}
void PrintError(unsigned,const char*,...){}
LONG WINAPI Crash(EXCEPTION_POINTERS* p){fprintf(stderr,"FAIL: fixture exception %08lX pc=%p RVA=%llX\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress,reinterpret_cast<uintptr_t>(p->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(image));ExitProcess(2);}
std::vector<unsigned char> ReadFileBytes(const fs::path& path){std::ifstream in(path,std::ios::binary);Check(bool(in),"read package fixture");return {std::istreambuf_iterator<char>(in),{}};}
void DecoderTests(const fs::path& package,const fs::path& previous,const std::string& map){
    using namespace decoderfixture;
    Copy(AuthInflateRva,AuthInflate);Copy(BlockDecodeRva,BlockDecode);Copy(ResetStateRva,ResetState);Copy(InitInflatorRva,InitInflator);Copy(ValidateHeaderRva,ValidateHeader);
    Commit(0x2458E30);memcpy(image+0x2458E30,"IWffa100",8);memcpy(image+0x2458E40,"IWffc100",8);
    Jump(0x21F64A0,reinterpret_cast<const void*>(&Memcpy));Jump(0x21F6CC0,reinterpret_cast<const void*>(&Memset));
    Jump(0x12B0570,reinterpret_cast<const void*>(&PrintError));Jump(0xD8A000,reinterpret_cast<const void*>(&InvalidFrame));
    auto init=reinterpret_cast<void(*)(void*,unsigned,const char*,void*)>(image+InitInflatorRva);
    auto inflate=reinterpret_cast<int(*)(void*)>(image+AuthInflateRva);
    auto header=reinterpret_cast<bool(*)(const void*,void*)>(image+ValidateHeaderRva);
    FlushInstructionCache(GetCurrentProcess(),image,0x1324B000);
    for(const char* prefix:{"techsets_","eng_","ww_","","srv_"}){
        const std::string filename=std::string(prefix)+map+".ff";const char* name=filename.c_str();
        auto file=ReadFileBytes(package/name);Check(file.size()>0x8C,"complete stored file");
        std::array<unsigned char,0x120> descriptor{};
        Check(header(file.data(),descriptor.data())&&descriptor[0x50]==0,"native header accepts IWffc100 and marks unsigned");
        uint64_t rawSize=0;memcpy(&rawSize,file.data()+0x20,8);Check(rawSize==file.size()-0x8C,"stored size matches body");
        std::vector<unsigned char> state(0x98400),output(static_cast<size_t>(rawSize),0xCC);
        init(state.data(),0,name,nullptr);
        Put(state,0x8E8E8,file.data()+0x88);Put<uint64_t>(state,0x8E8F0,file.size()-0x88);
        // Request small output chunks, as the real asset loader does.
        size_t done=0;
        while(done<output.size()){
            const size_t amount=(std::min)(size_t{17},output.size()-done);
            Put(state,0x8E900,output.data()+done);Put<uint64_t>(state,0x8E908,amount);
            Check(inflate(state.data())==0&&Get<uint64_t>(state,0x8E908)==0,"native stored decoder fills request");done+=amount;
        }
        Check(Get<uint64_t>(state,0x8E8F0)==0&&Get<uint64_t>(state,0x8E910)==rawSize&&memcmp(output.data(),file.data()+0x8C,output.size())==0,"native decoded package bytes match exact body");
        printf("PASS: Replay header and decoder %s (%llu body bytes, 17-byte requests)\n",name,rawSize);
    }
    auto old=ReadFileBytes(previous);std::array<unsigned char,0x120> descriptor{};
    Check(header(old.data(),descriptor.data())&&descriptor[0x50]==1,"previous file selects signed decoder");
    std::vector<unsigned char> state(0x98400),output(32,0xCC);
    Put<unsigned>(state,0x8E980,1);Put(state,0x8E8E8,old.data()+0x88);Put<uint64_t>(state,0x8E8F0,old.size()-0x88);
    Put(state,0x8E900,output.data());Put<uint64_t>(state,0x8E908,output.size());
    Check(inflate(state.data())==0&&Get<uint64_t>(state,0x8E8F0)==0&&Get<uint64_t>(state,0x8E908)==32&&Get<unsigned>(state,0x8E980+0x59BC)==old.size()-0x88,"previous framing exhausts input without producing any output");
    puts("PASS: reproduced previous signed-stream input exhaustion behind Disc error 5.0");
    std::fill(state.begin(),state.end(),0);unsigned char bad[]{1,'B','A','D',0};
    Put(state,0x8E8E8,bad);Put<uint64_t>(state,0x8E8F0,sizeof(bad));Put(state,0x8E900,output.data());Put<uint64_t>(state,0x8E908,output.size());
    Check(inflate(state.data())!=0&&invalidFrames==1,"native decoder rejects invalid resident marker");
}
void Prologue(const replay::Binding& binding,const std::vector<unsigned char>& undo,const void* callback){Commit(binding.rva);memcpy(image+binding.rva,binding.bytes,binding.size);memcpy(image+binding.rva+binding.size,undo.data(),undo.size());Jump(binding.rva+binding.size+undo.size(),callback);}
unsigned prints=0,discs=0,coms=0,fatals=0;
unsigned assetLinks=0;
uintptr_t NativeLinkAsset(int type,uintptr_t* header){Check(type==29&&assetcontext::current.active&&assetcontext::current.asset==*header&&assetcontext::current.shapeBytes==0,"asset context published before native link");Check(strcmp(assetcontext::current.name,"maps/mp/mp_test.d3dbsp")==0,"asset name captured");++assetLinks;SetLastError(0x4321);return 0x987;}
const char* expectedText;uintptr_t expectedReader;va_list expectedArgs;
uintptr_t NativePrint(unsigned channel,const char* text,int flags){Check(channel==10&&text==expectedText&&flags==3&&GetLastError()==0x1234,"print arguments and LastError forwarded");++prints;SetLastError(0x4321);return 0xABC;}
uintptr_t NativeDisc(uintptr_t reader,unsigned major,unsigned minor){Check(reader==expectedReader&&major==5&&minor==0&&GetLastError()==0x1234,"disc arguments and LastError forwarded");++discs;SetLastError(0x4321);return 0xDEF;}
void NativeCom(int code,const char* format,va_list args){Check(code==7&&format==expectedText&&args==expectedArgs&&GetLastError()==0x1234,"Com error va_list forwarded unchanged");Check(va_arg(args,int)==42&&strcmp(va_arg(args,const char*),"payload")==0,"native formatter receives original arguments");++coms;SetLastError(0x4321);}
void NativeFatal(const char* text){Check(text==expectedText&&GetLastError()==0x1234,"formatted fatal text forwarded unchanged");++fatals;SetLastError(0x4321);}
void InvokeCom(const char* format,...){va_list args;va_start(args,format);expectedArgs=args;expectedText=format;SetLastError(0x1234);reinterpret_cast<void(*)(int,const char*,va_list)>(image+replay::ComErrorInternal.rva)(7,format,args);va_end(args);Check(GetLastError()==0x4321,"Com error return LastError preserved");}
void DiagnosticTests(){
    // The real checked prologues lead to controlled continuations; no fatal game
    // implementation is executed. This checks detour relocation and forwarding.
    Prologue(replay::PrintMessage,{0x48,0x8B,0x5C,0x24,0x50,0x48,0x8B,0x74,0x24,0x58,0x48,0x83,0xC4,0x30,0x5F},reinterpret_cast<const void*>(&NativePrint));
    Prologue(replay::DiscError,{0x48,0x8B,0x5C,0x24,0x28,0x48,0x8B,0x6C,0x24,0x30,0x41,0x5E,0x5F,0x5E},reinterpret_cast<const void*>(&NativeDisc));
    Prologue(replay::ComErrorInternal,{0x48,0x8B,0x5C,0x24,0x40,0x48,0x83,0xC4,0x30,0x5F},reinterpret_cast<const void*>(&NativeCom));
    Commit(0xD38EB70);
    Prologue(replay::FatalText,{0x48,0x83,0xC4,0x28},reinterpret_cast<const void*>(&NativeFatal));
    Prologue(replay::LinkAssetEntry,{0x48,0x8B,0x5C,0x24,0x40,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F},reinterpret_cast<const void*>(&NativeLinkAsset));
    Check(enginediag::Initialize(GetModuleHandleW(nullptr)),"open engine test log");
    Check(enginediag::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"install four native diagnostic hooks");
    std::vector<unsigned char> mapEnts(0x428);const char* assetName="maps/mp/mp_test.d3dbsp";memcpy(mapEnts.data(),&assetName,sizeof(assetName));uintptr_t asset=reinterpret_cast<uintptr_t>(mapEnts.data());
    Check(reinterpret_cast<uintptr_t(*)(int,uintptr_t*)>(image+replay::LinkAssetEntry.rva)(29,&asset)==0x987&&GetLastError()==0x4321&&assetLinks==1&&!assetcontext::current.active,"native asset result preserved and outer context restored");
    expectedText="native error fixture\n";SetLastError(0x1234);
    Check(reinterpret_cast<uintptr_t(*)(unsigned,const char*,int)>(image+replay::PrintMessage.rva)(10,expectedText,3)==0xABC&&GetLastError()==0x4321,"print result preserved");
    std::vector<unsigned char> reader(0x120),descriptor(0x120),state(0x98400);
    strcpy_s(reinterpret_cast<char*>(descriptor.data()),descriptor.size(),"techsets_mp_test.ff");descriptor[0x50]=1;
    Put(reader,0,state.data());Put(reader,0xB0,descriptor.data());Put<uint64_t>(reader,0xC8,136);Put<uint64_t>(reader,0xD0,136);Put<uint64_t>(state,0x8E908,32);
    expectedReader=reinterpret_cast<uintptr_t>(reader.data());SetLastError(0x1234);
    Check(reinterpret_cast<uintptr_t(*)(uintptr_t,unsigned,unsigned)>(image+replay::DiscError.rva)(expectedReader,5,0)==0xDEF&&GetLastError()==0x4321,"disc result preserved");
    InvokeCom("number=%d text=%s",42,"payload");
    expectedText="Disc read error [5.0]: 'techsets_mp_test.ff'";SetLastError(0x1234);
    reinterpret_cast<void(*)(const char*)>(image+replay::FatalText.rva)(expectedText);Check(GetLastError()==0x4321,"fatal return LastError preserved");
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    auto bytes=ReadFileBytes(fs::path(path).parent_path()/"mw120rproxy.engine.log");std::string log(bytes.begin(),bytes.end());
    for(const char* expected:{"PRINT channel=0xA flags=3","DB_DiscError [5.0]","sizePassedToInflator=136 fileSize=136","avail_out=32","Com_Error_Internal code=7","Sys_Error formatted text:","frame[0]","ASSET BEGIN type=29","HavokBytes=0"})Check(log.find(expected)!=std::string::npos,"diagnostic log includes native message, decoder state and stack");
    Check(prints==1&&discs==1&&coms==1&&fatals==1,"all original error sinks called exactly once");
    puts("PASS: four real-prologue error hooks preserve native ABI, va_list, return values and LastError; flushed call-chain log verified");
}
}
void ConsoleGateTests(){
    engineconsole::Gate gate;
    const auto hash=engineconsole::Hash(14,3,"^1snapshot error\n");
    Check(gate.Accept(hash,true,1000),"first engine error displayed");
    Check(!gate.Accept(hash,true,1001),"repeated engine error throttled");
    Check(gate.Accept(hash,true,2000),"persistent engine error displayed again after interval");
    engineconsole::Gate burst;
    for(unsigned i=0;i<128;++i)Check(burst.Accept(i+1,false,1000),"normal console burst allowance");
    Check(!burst.Accept(10000,false,1000),"normal console flood bounded");
    Check(burst.Accept(10001,true,1000),"errors retain separate console allowance");
    Check(burst.Accept(10000,false,2000),"console burst allowance resets");
    puts("PASS: engine console duplicate filtering, bounded bursts and separate error allowance");
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,Crash);
    Check(argc==3 || argc==4,"usage: engine_tests <stored package> <previous signed techsets.ff> [map]");
    log120r::Init(GetModuleHandleW(nullptr),false);
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1324B000,MEM_RESERVE,PAGE_NOACCESS));Check(image!=nullptr,"reserve disposable Replay image");
    ConsoleGateTests();DecoderTests(argv[1],argv[2],argc>3?argv[3]:"mp_test");DiagnosticTests();return 0;
}
