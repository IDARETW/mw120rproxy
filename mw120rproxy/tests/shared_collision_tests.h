struct SharedShape {unsigned refs=1;std::vector<SharedShape*> children;};
unsigned sharedConstructed=0,sharedFreed=0,sharedInstances=0,sharedDestroyed=0;
std::vector<SharedShape*> sharedBodies[5];
void SharedRelease(void* pointer){auto* shape=static_cast<SharedShape*>(pointer);Check(shape->refs>0,"live shared shape reference");
 if(--shape->refs==0){for(auto* child:shape->children)SharedRelease(child);delete shape;++sharedFreed;}}
void* SharedBox(const float*,const float*){++sharedConstructed;return new SharedShape;}
void* SharedCompound(compoundcollision::Array* array){auto* shape=new SharedShape;++sharedConstructed;
 for(int i=0;i<array->size;++i){auto* child=static_cast<SharedShape*>(array->data[i].shape);++child->refs;shape->children.push_back(child);}return shape;}
unsigned SharedInstantiate(int world,const void* pointer,int ref,const char*,const char*,int contents,const float*,const float*,bool a,bool b,bool c){
 Check(ref==0&&contents==1&&a&&b&&!c,"shared shape instantiation retains native body ABI");
 auto* shape=(SharedShape*)pointer;++shape->refs;sharedBodies[world].push_back(shape);++sharedInstances;return unsigned(sharedBodies[world].size()-1);}
void SharedDestroy(int world,unsigned index,bool deferred){Check(!deferred,"synchronous shared body release");SharedRelease(sharedBodies[world][index]);++sharedDestroyed;}
uintptr_t SharedWorld(int world){return 100+world;}
void SharedCollisionTests(){
 const auto dir=packageRoot/id;{std::ofstream f(dir/"collision.bin",std::ios::binary);f.write("MWCOLL01",8);unsigned count=513;f.write((char*)&count,4);
  float box[]{0,0,0,32,32,32};for(unsigned i=0;i<count;++i)f.write((char*)box,24);}
 for(const auto* b:{&replay::CreateShapeAabb,&replay::CreateShapeConvex,&replay::CreateShapeCompound,&replay::InstantiateStaticBody,&replay::DestroyPhysicsInstance,&replay::RemoveHavokReference}){Commit(b->rva);memcpy(image+b->rva,b->bytes,b->size);}
 Commit(replay::WorldCollisionCreate.rva);Commit(replay::WorldCollisionShutdown.rva);
 PrologueFixture(replay::WorldCollisionCreate,{0x41,0x5D,0x41,0x5C,0x5F},reinterpret_cast<void*>(&SharedWorld));
 PrologueFixture(replay::WorldCollisionShutdown,{0x48,0x8B,0x7C,0x24,0x48,0x48,0x83,0xC4,0x28,0x5D,0x5B},reinterpret_cast<void*>(&SharedWorld));
 Check(customcollision::Install(reinterpret_cast<uintptr_t>(image))==hook::Status::Installed,"shared collision hooks validate native prologues");
 Stub(replay::CreateShapeAabb,&SharedBox);Stub(replay::CreateShapeCompound,&SharedCompound);Stub(replay::InstantiateStaticBody,&SharedInstantiate);
 Stub(replay::DestroyPhysicsInstance,&SharedDestroy);Stub(replay::RemoveHavokReference,&SharedRelease);
 *reinterpret_cast<uintptr_t*>(image+0xE5C62F8)=0;
 auto create=reinterpret_cast<uintptr_t(*)(int)>(image+replay::WorldCollisionCreate.rva);
 auto shutdown=reinterpret_cast<uintptr_t(*)(int)>(image+replay::WorldCollisionShutdown.rva);
 for(int world=0;world<5;++world)Check(create(world)==100+world,"original world creation result preserved");
 Check(sharedConstructed==516&&sharedInstances==15&&sharedFreed==0,"five worlds share 513 child shapes and three compounds");
 for(int world:{2,0,4,1})shutdown(world);
 Check(sharedFreed==0&&sharedDestroyed==12,"out-of-order shutdown retains shapes for remaining world");
 shutdown(3);Check(sharedFreed==516&&sharedDestroyed==15,"last world releases every constructor and body reference exactly once");
 for(auto& bodies:sharedBodies)bodies.clear();create(0);shutdown(0);
 Check(sharedConstructed==1032&&sharedFreed==1032,"subsequent map load creates fresh shapes without stale references");
 fs::remove(dir/"collision.bin");
 puts("PASS: actual shared collision implementation creates one shape set across five worlds; native call ABI, out-of-order shutdown, final release and reload verified (Havok allocator mocked)");
}
