unsigned ambientStock=0,ambientDispatch=0,ambientLocks=0;
std::array<unsigned char,64> expectedProbe{};
void AmbientStock(void*,const void*,const void*){++ambientStock;}
void* AmbientCompute(void* p){return p;}
void AmbientLock(uintptr_t p){Check(p==456,"ambient uses render context device");++ambientLocks;}
void AmbientUnlock(uintptr_t p){Check(p==456&&ambientLocks,"ambient unlock pairs with lock");--ambientLocks;}
void AmbientShader(void*,void* shader){Check(shader==reinterpret_cast<void*>(789),"ambient uses resident Replay fallback shader");}
void AmbientConstants(void*,unsigned slot,const void* data,unsigned size,void* optional){
 Check(slot==0&&size==96&&!optional,"ambient preserves native constant upload ABI");
 std::array<unsigned char,32> zeros{};Check(!memcmp(data,zeros.data(),32)&&!memcmp((char*)data+32,expectedProbe.data(),64),"native fallback branch receives exact probe with grid reads disabled");
}
void AmbientViews(void*,unsigned slot,unsigned count,const void* const* views,const unsigned* counters){Check(slot==0&&count==1&&!counters&&*views==image+0xFB94298,"ambient binds the original fallback UAV");}
void AmbientDispatch(void*,unsigned x,unsigned y,unsigned z){Check(x==1&&y==1&&z==1,"ambient dispatch contains one probe");++ambientDispatch;}
void CustomAmbientTests(){
 for(const auto* b:{&replay::GetComputeState,&replay::SetComputeShader,&replay::UploadComputeConstants,&replay::SetComputeRWViews,&replay::ComputeDispatch,&replay::LockGfxImmediate,&replay::UnlockGfxImmediate}){Commit(b->rva);memcpy(image+b->rva,b->bytes,b->size);}
 for(uintptr_t rva:{0x5A543F8,0xFB94258,0xFB94360,0x10C78D40,0x10C77870})Commit(rva);
 Commit(replay::UpdateFallbackProbe.rva);
 PrologueFixture(replay::UpdateFallbackProbe,{0x48,0x81,0xC4,0x08,0x01,0,0,0x41,0x5E,0x5F},reinterpret_cast<void*>(&AmbientStock));
 Check(customambient::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"ambient hook validates exact Replay entry points");
 Stub(replay::GetComputeState,&AmbientCompute);Stub(replay::SetComputeShader,&AmbientShader);Stub(replay::UploadComputeConstants,&AmbientConstants);Stub(replay::SetComputeRWViews,&AmbientViews);Stub(replay::ComputeDispatch,&AmbientDispatch);Stub(replay::LockGfxImmediate,&AmbientLock);Stub(replay::UnlockGfxImmediate,&AmbientUnlock);
 std::vector<unsigned char> backend(0x113168),state(0xC58);uintptr_t device=456,shader=789,resource=123;
 memcpy(state.data()+0xC50,&device,8);memcpy(image+0x10C78D40,&shader,8);memcpy(image+0xFB94258,&resource,8);
 unsigned count=0;memcpy(image+0xFB94360,&count,4);
 const std::string worldName="maps/mp/"+id+".d3dbsp";const char* name=worldName.c_str();auto* world=&name;memcpy(image+0x10C77870,&world,8);
 Check(custommaps::Select(id.c_str()),"ambient fixture selects installed package");
 const auto file=packageRoot/id/"ambient.bin";expectedProbe[0]=0x80;expectedProbe[1]=0x38;
 {std::ofstream out(file,std::ios::binary);out.write("MWRAMB01",8);out.write((char*)expectedProbe.data(),64);}
 auto update=reinterpret_cast<void(*)(void*,const void*,const void*)>(image+replay::UpdateFallbackProbe.rva);
 update(state.data(),backend.data(),nullptr);update(state.data(),backend.data(),nullptr);
 Check(ambientDispatch==1&&!ambientStock&&!ambientLocks,"empty custom world initializes ambient once with balanced context locking");
 custommaps::ClearSelection();update(state.data(),backend.data(),nullptr);
 Check(ambientStock==1&&image[0xFB942B8]==1,"stock transition restores native update and dirties stale custom probe");
 custommaps::Select(id.c_str());count=1;memcpy(image+0xFB94360,&count,4);update(state.data(),backend.data(),nullptr);
 Check(ambientStock==2&&ambientDispatch==1,"maps with native light grids keep native lighting");
 count=0;memcpy(image+0xFB94360,&count,4);fs::remove(file);memset(image+0x10C77870,0,8);
 puts("PASS: native ambient hook ABI, exact compute arguments, empty-grid scope, once-per-resource upload and stock-probe restoration (GPU submission mocked)");
}
