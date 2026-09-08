// Run the exact installed Replay fallback-probe compute shader on D3D12 WARP.
// No game process is opened or launched.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <fstream>
#include <vector>
#include <cstdio>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr,const char* what){if(FAILED(hr)){printf("FAIL %s HRESULT=%08X\n",what,unsigned(hr));ExitProcess(1);}}
int main(int argc,char** argv){
 if(argc!=3)return 2;
 std::ifstream f(argv[1],std::ios::binary);std::vector<char> shader((std::istreambuf_iterator<char>(f)),{});
 std::array<unsigned char,72> input{};std::ifstream p(argv[2],std::ios::binary);p.read((char*)input.data(),72);if(!p||memcmp(input.data(),"MWRAMB01",8))return 3;
 ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
 ComPtr<IDXGIAdapter> warp;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP");
 ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
 D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,9,0,0,1}};
 D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[0].Descriptor.ShaderRegister=0;
 params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
 D3D12_ROOT_SIGNATURE_DESC desc{2,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> bytes,error;
 Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error),"serialize root");
 ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root)),"root");
 D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root.Get();ps.CS={shader.data(),shader.size()};ComPtr<ID3D12PipelineState> pipeline;
 Check(device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline)),"actual Replay compute pipeline");
 auto buffer=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags){
  D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=256;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
  ComPtr<ID3D12Resource> r;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)),"buffer");return r;
 };
 auto constants=buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE);
 auto output=buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
 auto readback=buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_NONE);
 void* mapped=nullptr;Check(constants->Map(0,nullptr,&mapped),"constants map");memset(mapped,0,256);memcpy((char*)mapped+32,input.data()+8,64);constants->Unmap(0,nullptr);
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=10;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"heap");auto handle=heap->GetCPUDescriptorHandleForHeapStart();
 D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=1;u.Buffer.StructureByteStride=64;
 device->CreateUnorderedAccessView(output.Get(),nullptr,&u,handle);
 for(unsigned i=0;i<9;++i){handle.ptr+=device->GetDescriptorHandleIncrementSize(hd.Type);D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Buffer.NumElements=1;
  if(i>=4&&i<=6)s.Buffer.StructureByteStride=i==4?64:(i==5?12:16);else {s.Format=DXGI_FORMAT_R32_TYPELESS;s.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;}
  device->CreateShaderResourceView(nullptr,&s,handle);
 }
 D3D12_COMMAND_QUEUE_DESC q{};ComPtr<ID3D12CommandQueue> queue;Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
 ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
 ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pipeline.Get(),IID_PPV_ARGS(&list)),"list");
 list->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap* heaps[]{heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootConstantBufferView(0,constants->GetGPUVirtualAddress());list->SetComputeRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());list->Dispatch(1,1,1);
 D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
 list->CopyBufferRegion(readback.Get(),0,output.Get(),0,64);Check(list->Close(),"close");ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
 ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");Check(queue->Signal(fence.Get(),1),"signal");HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(fence->SetEventOnCompletion(1,event),"event");if(WaitForSingleObject(event,15000)!=WAIT_OBJECT_0)return 4;CloseHandle(event);
 Check(readback->Map(0,nullptr,&mapped),"readback");bool same=!memcmp(mapped,input.data()+8,64);readback->Unmap(0,nullptr);
 if(!same){puts("FAIL native shader probe readback mismatch");return 5;}
 puts("PASS: actual Replay cs_sh_sample_fallback_probe on D3D12 WARP copies all 64 sky-probe bytes with null light-grid SRVs");
}
