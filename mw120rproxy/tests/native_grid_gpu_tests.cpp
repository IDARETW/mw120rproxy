// Sample converted light grids with the exact Replay compute shader on D3D12 WARP.
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
#include <cmath>
#include <algorithm>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        printf("FAIL %s HRESULT=%08X\n", what, unsigned(hr));
        ExitProcess(1);
    }
}
float Half(unsigned short h) {
    const unsigned sign = (h & 0x8000u) << 16;
    unsigned bits = 0;
    if (h & 0x7c00u)
        bits = sign | (((h >> 10) & 31u) + 112u) << 23 | (h & 1023u) << 13;
    else
        return (h & 0x8000u ? -1.f : 1.f) * float(h & 1023u) / 16777216.f;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5)
        return 2;
    const bool packedSampling = argc == 5 && strcmp(argv[4], "--packed") == 0;
    if (argc == 5 && !packedSampling)
        return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<char> shader((std::istreambuf_iterator<char>(f)), {});
    std::ifstream gridFile(argv[2], std::ios::binary);
    std::vector<unsigned char> input((std::istreambuf_iterator<char>(gridFile)), {});
    std::ifstream queryFile(argv[3], std::ios::binary);
    std::vector<char> queryBytes((std::istreambuf_iterator<char>(queryFile)), {});
    if (input.size() < 264 || memcmp(input.data(), "IW8GLG01", 8) || queryBytes.empty() ||
        queryBytes.size() % 24)
        return 3;
    std::array<unsigned, 4> counts{};
    memcpy(counts.data(), input.data() + 8, 16);
    const unsigned queryCount = unsigned(queryBytes.size() / 24);
    const unsigned sampleCount = queryCount + 127;
    const unsigned srvCount = packedSampling ? 18 : 9;
    const unsigned uavCount = packedSampling ? 2 : 1;
    const unsigned outputSize = packedSampling ? sampleCount * 32 : 64;
    if (queryCount > 1024)
        return 3;
    const auto [probes, tets, roots, voxels] = counts;
    size_t cursor = 264;
    std::array<std::vector<unsigned char>, 18> resources;
    for (auto [slot, bytes] :
         {std::pair(0, size_t(probes) * 32), std::pair(1, size_t(probes) * 12),
          std::pair(2, size_t(tets) * 16), std::pair(3, size_t(tets) * 16),
          std::pair(5, size_t(roots) * 12), std::pair(7, size_t(voxels) * 4)}) {
        if (bytes > input.size() - cursor)
            return 3;
        resources[slot].assign(input.begin() + cursor, input.begin() + cursor + bytes);
        cursor += bytes;
    }
    if (cursor != input.size())
        return 3;
    resources[4].assign(input.begin() + 200, input.begin() + 264);
    resources[6].resize(16);
    resources[8].resize(64);
    if (packedSampling) {
        resources[9].assign(input.begin() + 136, input.begin() + 200);
        for (unsigned i = 10; i < 17; ++i)
            resources[i].resize(sampleCount * 32);
        resources[15].assign(sampleCount * 4, 0xff);
        resources[17].resize(sampleCount * 16);
        for (unsigned i = 0; i < queryCount; ++i) {
            auto* position = resources[17].data() + (i + 127) * 16;
            memcpy(position, queryBytes.data() + i * 24, 12);
            memset(position + 12, 0xff, 4);
        }
    }
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
    ComPtr<IDXGIAdapter> warp;
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP");
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "device");
    D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, 0},
                                     {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, uavCount}};
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {2, ranges};
    D3D12_ROOT_SIGNATURE_DESC desc{2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> bytes, error;
    Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &error),
          "serialize root");
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(),
                                      IID_PPV_ARGS(&root)),
          "root");
    D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};
    ps.pRootSignature = root.Get();
    ps.CS = {shader.data(), shader.size()};
    ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&pipeline)),
          "actual Replay compute pipeline");
    auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags,
                      size_t size = 256) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = type;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = (std::max)(size_t(256), size);
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = flags;
        ComPtr<ID3D12Resource> r;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr,
                                              IID_PPV_ARGS(&r)),
              "buffer");
        return r;
    };
    auto constants = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                            D3D12_RESOURCE_FLAG_NONE, queryCount * 256);
    auto output = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, outputSize);
    auto sampledTets = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, sampleCount * 4);
    auto readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_FLAG_NONE, packedSampling ? outputSize : queryCount * 64);
    void* mapped = nullptr;
    Check(constants->Map(0, nullptr, &mapped), "constants map");
    memset(mapped, 0, queryCount * 256);
    for (unsigned i = 0; !packedSampling && i < queryCount; ++i) {
        char* block = static_cast<char*>(mapped) + i * 256;
        memcpy(block, queryBytes.data() + i * 24, 12);
        const unsigned useSample = 1;
        memcpy(block + 28, &useSample, 4);
        memcpy(block + 32, input.data() + 136, 64);
    }
    if (packedSampling) {
        unsigned values[8]{0, sampleCount, 0, 0, UINT_MAX, 0, 0, 0};
        memcpy(mapped, values, sizeof(values));
    }
    constants->Unmap(0, nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = uavCount + srvCount;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "heap");
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = 1;
    u.Buffer.StructureByteStride = 64;
    if (packedSampling) {
        u.Format = DXGI_FORMAT_R32_TYPELESS;
        u.Buffer.StructureByteStride = 0;
        u.Buffer.NumElements = outputSize / 4;
        u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    }
    device->CreateUnorderedAccessView(output.Get(), nullptr, &u, handle);
    if (packedSampling) {
        handle.ptr += device->GetDescriptorHandleIncrementSize(hd.Type);
        u.Format = DXGI_FORMAT_UNKNOWN;
        u.Buffer.StructureByteStride = 4;
        u.Buffer.NumElements = sampleCount;
        u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(sampledTets.Get(), nullptr, &u, handle);
    }
    std::array<ComPtr<ID3D12Resource>, 18> uploads;
    for (unsigned i = 0; i < srvCount; ++i) {
        handle.ptr += device->GetDescriptorHandleIncrementSize(hd.Type);
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Buffer.NumElements = 1;
        if (i >= 4 && i <= 6)
            s.Buffer.StructureByteStride = i == 4 ? 64 : (i == 5 ? 12 : 16);
        else if (i == 9 || i == 15 || i == 17)
            s.Buffer.StructureByteStride = i == 9 ? 64 : (i == 15 ? 4 : 16);
        else {
            s.Format = DXGI_FORMAT_R32_TYPELESS;
            s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        }
        uploads[i] = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                            D3D12_RESOURCE_FLAG_NONE, resources[i].size());
        Check(uploads[i]->Map(0, nullptr, &mapped), "grid upload");
        memcpy(mapped, resources[i].data(), resources[i].size());
        uploads[i]->Unmap(0, nullptr);
        s.Buffer.NumElements =
            unsigned(resources[i].size() /
                     (s.Buffer.StructureByteStride ? s.Buffer.StructureByteStride : 4));
        device->CreateShaderResourceView(uploads[i].Get(), &s, handle);
    }
    D3D12_COMMAND_QUEUE_DESC q{};
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue");
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
          "allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                    pipeline.Get(), IID_PPV_ARGS(&list)),
          "list");
    list->SetComputeRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[]{heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress());
    list->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
    for (unsigned i = 0; i < (packedSampling ? 1 : queryCount); ++i) {
        list->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress() + i * 256);
        list->Dispatch(packedSampling ? (sampleCount + 63) / 64 : 1, 1, 1);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &barrier);
        list->CopyBufferRegion(readback.Get(), i * 64, output.Get(), 0, outputSize);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
    }
    Check(list->Close(), "close");
    ID3D12CommandList* lists[]{list.Get()};
    queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    Check(queue->Signal(fence.Get(), 1), "signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(fence->SetEventOnCompletion(1, event), "event");
    if (WaitForSingleObject(event, 15000) != WAIT_OBJECT_0)
        return 4;
    CloseHandle(event);
    Check(readback->Map(0, nullptr, &mapped), "readback");
    unsigned failures = 0;
    for (unsigned i = 0; i < queryCount; ++i) {
        float expected[3];
        memcpy(expected, queryBytes.data() + i * 24 + 12, 12);
        const auto* result = static_cast<const unsigned char*>(mapped) +
                             (packedSampling ? (i + 127) * 32 : i * 64);
        if (packedSampling && result[29] != 255) {
            if (failures++ < 12)
                printf("FAIL query %u normalization expected 255 actual %u\n", i, result[29]);
        }
        for (unsigned channel = 0; channel < 3; ++channel) {
            unsigned short packed = 0;
            if (packedSampling) {
                unsigned dc = 0;
                memcpy(&dc, result, 4);
                packed = channel == 0 ? (dc & 2047) << 4 :
                         channel == 1 ? ((dc >> 11) & 2047) << 4 : (dc >> 22) << 5;
            } else
                memcpy(&packed, result + channel * 18, 2);
            const float actual = Half(packed);
            // The shader stores binary16 coefficients. Allow one output ULP;
            // an absolute 0.003 threshold is smaller than one step above 4.0.
            int exponent = 0;
            std::frexp(std::abs(expected[channel]), &exponent);
            const int precision = packedSampling ? (channel == 2 ? 6 : 7) : 11;
            const float tolerance = std::max(0.003f, std::ldexp(1.0f, exponent - precision));
            if (!std::isfinite(actual) || std::abs(actual - expected[channel]) > tolerance) {
                if (failures++ < 12)
                    printf("FAIL query %u channel %u expected %.6f actual %.6f\n", i, channel,
                           expected[channel], actual);
            }
        }
    }
    readback->Unmap(0, nullptr);
    if (failures)
        return 5;
    printf("PASS: %u spatial probes sampled by Replay's %s shader on D3D12 WARP\n",
           queryCount, packedSampling ? "regular packed-probe" : "fallback-probe");
}
