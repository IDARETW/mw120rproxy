std::vector<std::string> noclipArgs;
unsigned noclipForwarded=0;
bool noclipLocal=true;
void NoclipArgv(int index,char* buffer,size_t size){strncpy_s(buffer,size,index<int(noclipArgs.size())?noclipArgs[index].c_str():"",_TRUNCATE);}
void NoclipFallback(int){++noclipForwarded;}
bool NoclipLocal(const char* token){Check(!strcmp(token,"LPSPMQSNPQ"),"noclip checks Local Play");return noclipLocal;}
void NoclipTests(){
    for(const auto* b:{&replay::ServerArgv,&replay::NoclipMovementFlags,&replay::IsFrontEnd,&replay::GetBool,&replay::ClientCommand}) {
        Commit(b->rva);memcpy(image+b->rva,b->bytes,b->size);
    }
    PrologueFixture(replay::ClientCommand,{0x41,0x5F,0x41,0x5E,0x5E,0x5D},reinterpret_cast<void*>(&NoclipFallback));
    Check(noclip::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"noclip server command and exact movement flag bindings install");
    Stub(replay::ServerArgv,&NoclipArgv);Stub(replay::IsFrontEnd,&Frontend);Stub(replay::GetBool,&NoclipLocal);
    Commit(0xBC20F00);
    std::array<unsigned char,0x5A0> entity{};std::array<unsigned char,0x6000> player{};
    auto ent=reinterpret_cast<uintptr_t>(entity.data()),client=reinterpret_cast<uintptr_t>(player.data());
    memcpy(image+0xBC20F00,&ent,8);memcpy(entity.data()+0x150,&client,8);
    auto& flags=*reinterpret_cast<unsigned*>(player.data()+0x5DD0);
    auto& pm=*reinterpret_cast<int*>(player.data()+0xC);
    auto dispatch=reinterpret_cast<void(*)(int)>(image+replay::ClientCommand.rva);
    flags=0x204;pm=0;frontend=false;noclipArgs={"noclip"};dispatch(0);
    Check(flags==0x205 && pm==0,"noclip toggles only native server flag; stock movement owns pm_type");
    // Execute Replay's actual flag-to-pm_type instructions; replace only its
    // following jump with padding/return so no full server world is required.
    auto* native=static_cast<unsigned char*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(native!=nullptr,"allocate native noclip flag fixture");
    const unsigned char setup[]{0x56,0x48,0x8B,0xF1};memcpy(native,setup,4);
    memcpy(native+4,replay::NoclipMovementFlags.bytes,17);memset(native+21,0x90,5);native[26]=0x5E;native[27]=0xC3;
    reinterpret_cast<void(*)(void*)>(native)(player.data());Check(pm==2,"actual Replay flag branch selects noclip movement");
    noclipArgs={"noclip","on"};dispatch(0);Check(flags==0x205,"explicit noclip on is idempotent");
    noclipArgs={"noclip","off"};dispatch(0);Check(flags==0x204,"noclip off preserves all other flags");
    pm=0;reinterpret_cast<void(*)(void*)>(native)(player.data());Check(pm==0,"cleared bit does not choose noclip movement");VirtualFree(native,0,MEM_RELEASE);
    for(auto args:std::vector<std::vector<std::string>>{{"noclip","maybe"},{"noclip","on","extra"}}){noclipArgs=args;dispatch(0);Check(flags==0x204,"invalid noclip arguments leave state unchanged");}
    noclipArgs={"noclip","1"};frontend=true;dispatch(0);Check(flags==0x204,"frontend refuses noclip");frontend=false;
    noclipLocal=false;dispatch(0);Check(flags==0x204,"non-local sessions refuse noclip");noclipLocal=true;
    dispatch(1);Check(flags==0x204,"noclip cannot target another client");
    pm=7;dispatch(0);Check(flags==0x204,"unspawned/dead movement refuses noclip");pm=0;
    uintptr_t absent=0;memcpy(entity.data()+0x150,&absent,8);dispatch(0);Check(flags==0x204,"missing player refuses noclip");memcpy(entity.data()+0x150,&client,8);
    noclipArgs={"say","hello"};dispatch(0);Check(noclipForwarded==1,"unrelated native server commands retain original path");
    memcpy(image+0xBC20F00,&absent,8);Stub(replay::GetBool,&Bool);frontend=true;
    puts("PASS: noclip server hook ABI, native movement flag execution, toggle/on/off, argument validation, Local Play and spawn guards");
}
