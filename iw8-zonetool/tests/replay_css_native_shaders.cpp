#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
void check(const HRESULT result, const char *operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 std::to_string(static_cast<unsigned>(result)));
}

std::vector<char> readShader(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open " + path.string());
    std::vector<char> bytes{std::istreambuf_iterator<char>{file},
                            std::istreambuf_iterator<char>{}};
    if (bytes.empty())
        throw std::runtime_error("empty shader " + path.string());
    return bytes;
}

std::vector<float> readDepthImage(const std::filesystem::path &path,
                                  const size_t resolution = 512)
{
    const size_t count = resolution * resolution;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(count * sizeof(float)))
        throw std::runtime_error("depth input must be " + std::to_string(resolution) +
                                 "x" + std::to_string(resolution) +
                                 " R32_FLOAT: " + path.string());
    std::vector<float> pixels(count);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(pixels.data()),
                    static_cast<std::streamsize>(count * sizeof(float))))
        throw std::runtime_error("cannot read depth input " + path.string());
    for (const float pixel : pixels)
        if (!std::isfinite(pixel) || pixel < 0.0f || pixel > 1.0f)
            throw std::runtime_error("depth input is outside [0,1]: " + path.string());
    return pixels;
}

ComPtr<ID3D12Resource> createBuffer(ID3D12Device *device, const UINT64 size,
                                    const D3D12_HEAP_TYPE heapType,
                                    const D3D12_RESOURCE_STATES initialState,
                                    const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;
    ComPtr<ID3D12Resource> resource;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                         initialState, nullptr,
                                         IID_PPV_ARGS(&resource)), "create buffer");
    return resource;
}

ComPtr<ID3D12Resource> createDepthTexture(ID3D12Device *device,
                                          const UINT resolution = 512,
                                          const UINT16 mipLevels = 1)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = resolution;
    description.Height = resolution;
    description.DepthOrArraySize = 1;
    description.MipLevels = mipLevels;
    description.Format = DXGI_FORMAT_R32_FLOAT;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> resource;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         nullptr, IID_PPV_ARGS(&resource)),
          "create R32_FLOAT depth texture");
    return resource;
}

void waitForGpu(ID3D12CommandQueue *queue, ID3D12Device *device)
{
    ComPtr<ID3D12Fence> fence;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "create fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event)
        throw std::runtime_error("create fence event failed");
    const auto closeEvent = [&] { CloseHandle(event); };
    constexpr UINT64 value = 1;
    HRESULT result = queue->Signal(fence.Get(), value);
    if (FAILED(result))
    {
        closeEvent();
        check(result, "signal fence");
    }
    result = fence->SetEventOnCompletion(value, event);
    if (FAILED(result))
    {
        closeEvent();
        check(result, "set fence event");
    }
    const DWORD waitResult = WaitForSingleObject(event, 15000);
    closeEvent();
    if (waitResult != WAIT_OBJECT_0)
        throw std::runtime_error(waitResult == WAIT_TIMEOUT
                                     ? "timed out waiting for WARP queue"
                                     : "wait for WARP queue failed");
}

void dispatchDepthPair(ID3D12Device *device, ID3D12RootSignature *root,
                        ID3D12PipelineState *pipeline, const float depth0,
                        const float depth1, const float expectedFirst,
                        const float expectedSecond)
{
    constexpr UINT kScratchNodes = (512u * 512u - 4u) / 3u;
    constexpr UINT64 kScratchBytes = UINT64(kScratchNodes) * 16u;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    const HRESULT queueResult =
        device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue));
    if (FAILED(queueResult))
        throw std::runtime_error("create direct queue failed: " +
                                 std::to_string(static_cast<unsigned>(queueResult)) +
                                 ", removed reason=" +
                                 std::to_string(static_cast<unsigned>(device->GetDeviceRemovedReason())));

    constexpr UINT descriptorCount = 6;
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.NumDescriptors = descriptorCount;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    check(device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&descriptors)),
          "create process_nodes descriptor heap");
    const UINT descriptorStride =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto cpuAt = [&](const UINT index) {
        auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * descriptorStride;
        return handle;
    };
    const auto gpuAt = [&](const UINT index) {
        auto handle = descriptors->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * descriptorStride;
        return handle;
    };

    std::array<ComPtr<ID3D12Resource>, 2> depthTextures{
        createDepthTexture(device), createDepthTexture(device)};
    check(device->GetDeviceRemovedReason(), "WARP device after depth texture creation");
    ComPtr<ID3D12Resource> nodes = createBuffer(
        device, kScratchBytes, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> zeroUpload = createBuffer(
        device, kScratchBytes, D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    check(device->GetDeviceRemovedReason(), "WARP device after node buffer creation");
    void *mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    check(zeroUpload->Map(0, &noRead, &mapped), "map zero node upload");
    std::memset(mapped, 0, static_cast<size_t>(kScratchBytes));
    zeroUpload->Unmap(0, nullptr);

    ComPtr<ID3D12Resource> constants = createBuffer(
        device, 256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    struct alignas(16) Constants
    {
        std::uint32_t setup[4];
        std::uint32_t layer[3];
        float reciprocalResolution;
        std::uint32_t downsampleRatio;
        float reciprocalDownsampledResolution;
        std::uint32_t padding[2];
    };
    static_assert(sizeof(Constants) == 48);
    const Constants values{{512, 9, kScratchNodes, 0},
                           {0, 1, 0}, 1.0f / 512.0f,
                           1, 1.0f / 512.0f, {0, 0}};
    check(constants->Map(0, &noRead, &mapped), "map compressor constants");
    std::memcpy(mapped, &values, sizeof(values));
    constants->Unmap(0, nullptr);

    D3D12_UNORDERED_ACCESS_VIEW_DESC textureUav{};
    textureUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    textureUav.Format = DXGI_FORMAT_R32_FLOAT;
    for (UINT index = 0; index != depthTextures.size(); ++index)
    {
        device->CreateUnorderedAccessView(depthTextures[index].Get(), nullptr,
                                          &textureUav, cpuAt(index + 2));
        check(device->GetDeviceRemovedReason(), "WARP device after texture UAV creation");
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC nodeUav{};
    nodeUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    nodeUav.Format = DXGI_FORMAT_UNKNOWN;
    nodeUav.Buffer.NumElements = kScratchNodes;
    nodeUav.Buffer.StructureByteStride = 16;
    device->CreateUnorderedAccessView(nodes.Get(), nullptr, &nodeUav, cpuAt(4));
    check(device->GetDeviceRemovedReason(), "WARP device after node UAV creation");
    device->CreateUnorderedAccessView(nodes.Get(), nullptr, &nodeUav, cpuAt(5));
    check(device->GetDeviceRemovedReason(), "WARP device after secondary UAV creation");

    ComPtr<ID3D12CommandAllocator> allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator)),
          "create command allocator");
    ComPtr<ID3D12GraphicsCommandList> commands;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&commands)),
          "create command list");

    const std::array<float, 2> depths{depth0, depth1};
    for (UINT index = 0; index != depthTextures.size(); ++index)
    {
        const std::array<float, 4> clearValues{depths[index], 0, 0, 0};
        commands->ClearUnorderedAccessViewFloat(gpuAt(index + 2), cpuAt(index + 2),
                                                depthTextures[index].Get(),
                                                clearValues.data(), 0, nullptr);
    }
    commands->CopyBufferRegion(nodes.Get(), 0, zeroUpload.Get(), 0, kScratchBytes);
    std::array<D3D12_RESOURCE_BARRIER, 3> barriers{};
    for (UINT index = 0; index != depthTextures.size(); ++index)
    {
        barriers[index].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[index].Transition.pResource = depthTextures[index].Get();
        barriers[index].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[index].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[index].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = nodes.Get();
    barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    D3D12_SHADER_RESOURCE_VIEW_DESC textureSrv{};
    textureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textureSrv.Format = DXGI_FORMAT_R32_FLOAT;
    textureSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depthTextures[0].Get(), &textureSrv, cpuAt(0));
    device->CreateShaderResourceView(depthTextures[1].Get(), &textureSrv, cpuAt(1));

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    readbackHeap.CreationNodeMask = 1;
    readbackHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC readbackDescription{};
    readbackDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDescription.Width = 16;
    readbackDescription.Height = 1;
    readbackDescription.DepthOrArraySize = 1;
    readbackDescription.MipLevels = 1;
    readbackDescription.SampleDesc.Count = 1;
    readbackDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    check(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE,
                                         &readbackDescription,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&readback)),
          "create readback buffer");

    ID3D12DescriptorHeap *descriptorHeap[] = {descriptors.Get()};
    commands->SetDescriptorHeaps(1, descriptorHeap);
    commands->SetComputeRootSignature(root);
    commands->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress());
    commands->SetComputeRootDescriptorTable(1, gpuAt(0));
    commands->SetComputeRootDescriptorTable(2, gpuAt(4));
    commands->SetPipelineState(pipeline);
    commands->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER nodeBarrier{};
    nodeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    nodeBarrier.Transition.pResource = nodes.Get();
    nodeBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    nodeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    nodeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &nodeBarrier);
    commands->CopyBufferRegion(readback.Get(), 0, nodes.Get(), 0, 16);
    check(commands->Close(), "close process_nodes command list");
    ID3D12CommandList *lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    waitForGpu(queue.Get(), device);
    check(device->GetDeviceRemovedReason(), "WARP device after process_nodes dispatch");

    struct Node
    {
        float firstDepth;
        float secondDepth;
        std::uint32_t flags;
        std::uint32_t childOffset;
    } node{};
    D3D12_RANGE readRange{0, sizeof(node)};
    check(readback->Map(0, &readRange, &mapped), "map process_nodes readback");
    std::memcpy(&node, mapped, sizeof(node));
    D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    if (std::isfinite(expectedFirst) &&
        (std::abs(node.firstDepth - expectedFirst) > 1e-6f ||
         std::abs(node.secondDepth - expectedSecond) > 1e-6f ||
         node.flags != 0xF02u || node.childOffset != 0))
        throw std::runtime_error("process_nodes depth-pair output did not match: " +
                                 std::to_string(node.firstDepth) + ", " +
                                 std::to_string(node.secondDepth) + ", flags=" +
                                 std::to_string(node.flags) + ", child=" +
                                 std::to_string(node.childOffset));
    std::cout << "process_nodes depth pair t0=" << depth0 << ", t1=" << depth1
              << ", res=512, layer=0, nodes=1"
              << (std::isfinite(expectedFirst) ? " passed: " : " observed: ")
              << "node={" << node.firstDepth << ", " << node.secondDepth
              << ", 0x" << std::hex << node.flags << ", 0x" << node.childOffset
              << std::dec << "}\n";
}

void dispatchFullTile(ID3D12Device *device, ID3D12RootSignature *root,
                      ID3D12PipelineState *preparePipeline,
                      ID3D12PipelineState *processPipeline,
                      ID3D12PipelineState *resetPipeline,
                      ID3D12PipelineState *serializePipeline,
                      const std::filesystem::path &outputPath,
                      const unsigned depthMode,
                      const std::array<std::filesystem::path, 2> *inputPaths = nullptr)
{
    constexpr std::uint32_t resolution = 512;
    constexpr std::uint32_t layerCount = 9;
    constexpr std::uint32_t scratchNodeCount = 87380;
    // serialize_tree stores per-layer bookkeeping immediately after scratch nodes.
    constexpr std::uint32_t nodeCapacity = scratchNodeCount + layerCount;
    // Dense native trees can exceed one byte per input pixel; the shipped
    // mini-tile oracle reaches 731,172 bytes before sparse test inputs do.
    constexpr std::uint32_t outputCapacity = 2u * 1024u * 1024u;
    constexpr std::array<std::uint32_t, layerCount> nodeCounts{
        65536, 16384, 4096, 1024, 256, 64, 16, 4, 262144};
    constexpr std::array<std::uint32_t, layerCount> nodeOffsets{
        0, 65536, 81920, 86016, 87040, 87296, 87360, 87376, 0};
    constexpr UINT64 nodeBytes = UINT64(nodeCapacity) * 16;
    constexpr UINT64 constantStride = 256;

    enum class StageKind
    {
        process,
        prepare,
        serialize,
        reset,
    };
    struct Stage
    {
        StageKind kind;
        std::uint32_t layer;
        UINT groups;
    };
    std::vector<Stage> stages;
    const auto addStage = [&](const StageKind kind, const std::uint32_t layer,
                              const std::uint32_t nodesPerGroup)
    {
        // ResetDataLayout clears all remaining scratch records starting at
        // this layer's offset, not just the layer's parent groups. The native
        // host dispatches ceil((totalNodes - layerOffset) / 64) groups.
        const std::uint32_t invocations = kind == StageKind::reset
            ? scratchNodeCount - nodeOffsets[layer]
            : nodesPerGroup == 1 ? nodeCounts[layer] : nodeCounts[layer] >> 2;
        stages.push_back({kind, layer, (invocations + 63) / 64});
    };
    for (std::uint32_t layer = 0; layer + 1 < layerCount; ++layer)
        addStage(StageKind::process, layer, 1);
    addStage(StageKind::serialize, layerCount - 2, 4);
    for (int layer = static_cast<int>(layerCount) - 3; layer >= 0; --layer)
    {
        addStage(StageKind::prepare, static_cast<std::uint32_t>(layer), 4);
        addStage(StageKind::serialize, static_cast<std::uint32_t>(layer), 4);
        addStage(StageKind::reset, static_cast<std::uint32_t>(layer), 4);
    }
    addStage(StageKind::prepare, layerCount - 1, 4);
    addStage(StageKind::serialize, layerCount - 1, 4);
    addStage(StageKind::reset, layerCount - 1, 4);

    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)),
          "create full-tile direct queue");

    constexpr UINT descriptorCount = 6;
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.NumDescriptors = descriptorCount;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    check(device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&descriptors)),
          "create full-tile descriptor heap");
    const UINT descriptorStride =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto cpuAt = [&](const UINT index)
    {
        auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * descriptorStride;
        return handle;
    };
    const auto gpuAt = [&](const UINT index)
    {
        auto handle = descriptors->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * descriptorStride;
        return handle;
    };

    const std::uint32_t depthResolution = depthMode == 11 ? 1024 : resolution;
    const std::uint32_t downsampleRatio = depthResolution / resolution;
    std::array<ComPtr<ID3D12Resource>, 2> depthTextures{
        createDepthTexture(device, depthResolution),
        createDepthTexture(device, depthResolution)};
    std::array<ComPtr<ID3D12Resource>, 2> depthUploads;
    if (inputPaths)
    {
        for (size_t index = 0; index != depthUploads.size(); ++index)
        {
            const auto pixels = readDepthImage((*inputPaths)[index], depthResolution);
            depthUploads[index] = createBuffer(
                device, pixels.size() * sizeof(float), D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_STATE_GENERIC_READ);
            void *upload = nullptr;
            const D3D12_RANGE noRead{0, 0};
            check(depthUploads[index]->Map(0, &noRead, &upload),
                  "map full-tile depth input");
            std::memcpy(upload, pixels.data(), pixels.size() * sizeof(float));
            depthUploads[index]->Unmap(0, nullptr);
        }
    }
    ComPtr<ID3D12Resource> nodes = createBuffer(
        device, nodeBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> nodeUpload = createBuffer(
        device, nodeBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void *mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    check(nodeUpload->Map(0, &noRead, &mapped), "map full-tile node initialization");
    std::memset(mapped, 0, static_cast<size_t>(nodeBytes));
    nodeUpload->Unmap(0, nullptr);
    ComPtr<ID3D12Resource> rawOutput = createBuffer(
        device, outputCapacity, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> constants = createBuffer(
        device, constantStride * stages.size(), D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    struct alignas(16) Constants
    {
        std::uint32_t setup[4];
        std::uint32_t layer[3];
        float reciprocalResolution;
        std::uint32_t downsampleRatio;
        float reciprocalDownsampledResolution;
        std::uint32_t padding[2];
    };
    static_assert(sizeof(Constants) == 48);
    check(constants->Map(0, &noRead, &mapped), "map full-tile compressor constants");
    for (size_t index = 0; index != stages.size(); ++index)
    {
        const std::uint32_t layer = stages[index].layer;
        const Constants values{{resolution, layerCount, scratchNodeCount, outputCapacity},
                               {layer, nodeCounts[layer], nodeOffsets[layer]},
                               1.0f / resolution, downsampleRatio,
                               1.0f / depthResolution, {0, 0}};
        std::memcpy(static_cast<std::byte *>(mapped) + index * constantStride,
                    &values, sizeof(values));
    }
    constants->Unmap(0, nullptr);

    D3D12_UNORDERED_ACCESS_VIEW_DESC textureUav{};
    textureUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    textureUav.Format = DXGI_FORMAT_R32_FLOAT;
    for (UINT index = 0; index != depthTextures.size(); ++index)
        device->CreateUnorderedAccessView(depthTextures[index].Get(), nullptr,
                                          &textureUav, cpuAt(index + 2));

    D3D12_UNORDERED_ACCESS_VIEW_DESC nodeUav{};
    nodeUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    nodeUav.Format = DXGI_FORMAT_UNKNOWN;
    nodeUav.Buffer.NumElements = nodeCapacity;
    nodeUav.Buffer.StructureByteStride = 16;
    device->CreateUnorderedAccessView(nodes.Get(), nullptr, &nodeUav, cpuAt(4));
    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUav{};
    rawUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUav.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUav.Buffer.NumElements = outputCapacity / sizeof(std::uint32_t);
    rawUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(rawOutput.Get(), nullptr, &rawUav, cpuAt(5));

    ComPtr<ID3D12CommandAllocator> allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator)),
          "create full-tile command allocator");
    ComPtr<ID3D12GraphicsCommandList> commands;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&commands)),
          "create full-tile command list");
    for (UINT index = 0; index != depthTextures.size(); ++index)
    {
        if (inputPaths)
        {
            D3D12_RESOURCE_BARRIER copyBarrier{};
            copyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copyBarrier.Transition.pResource = depthTextures[index].Get();
            copyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            copyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            copyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            commands->ResourceBarrier(1, &copyBarrier);
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = depthTextures[index].Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = depthUploads[index].Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
            source.PlacedFootprint.Footprint.Width = depthResolution;
            source.PlacedFootprint.Footprint.Height = depthResolution;
            source.PlacedFootprint.Footprint.Depth = 1;
            source.PlacedFootprint.Footprint.RowPitch = depthResolution * sizeof(float);
            commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            continue;
        }
        const std::array<float, 4> depthClear{
            depthMode == 8 ? (index == 0 ? 0.0f : 1.0f) :
            depthMode == 9 ? 0.0f : 0.5f,
            0, 0, 0};
        commands->ClearUnorderedAccessViewFloat(gpuAt(index + 2), cpuAt(index + 2),
                                                depthTextures[index].Get(),
                                                depthClear.data(), 0, nullptr);
        if (depthMode)
        {
            const std::array<float, 4> nearDepth{
                (depthMode == 8 || depthMode == 9) && index == 0 ? 0.75f :
                depthMode == 2 && index == 1 ? 0.75f : 0.25f,
                0, 0, 0};
            const int quadrantX = depthMode >= 4 && depthMode < 8
                ? int((depthMode - 4) & 1) * 256 : 0;
            const int quadrantY = depthMode >= 4 && depthMode < 8
                ? int((depthMode - 4) >> 1) * 256 : 0;
            const D3D12_RECT center = depthMode >= 4 && depthMode < 8
                ? D3D12_RECT{quadrantX, quadrantY, quadrantX + 256, quadrantY + 256}
                : D3D12_RECT{128, 128, 384, 384};
            commands->ClearUnorderedAccessViewFloat(gpuAt(index + 2), cpuAt(index + 2),
                                                    depthTextures[index].Get(),
                                                    nearDepth.data(), 1, &center);
            if (depthMode == 3)
            {
                const std::array<float, 4> insetDepth{0.75f, 0, 0, 0};
                const D3D12_RECT inset{160, 160, 224, 224};
                commands->ClearUnorderedAccessViewFloat(gpuAt(index + 2), cpuAt(index + 2),
                                                        depthTextures[index].Get(),
                                                        insetDepth.data(), 1, &inset);
            }
        }
    }
    const std::array<std::uint32_t, 4> zero{};
    commands->ClearUnorderedAccessViewUint(gpuAt(5), cpuAt(5), rawOutput.Get(),
                                           zero.data(), 0, nullptr);
    commands->CopyBufferRegion(nodes.Get(), 0, nodeUpload.Get(), 0, nodeBytes);

    std::array<D3D12_RESOURCE_BARRIER, 3> initialBarriers{};
    for (UINT index = 0; index != depthTextures.size(); ++index)
    {
        initialBarriers[index].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        initialBarriers[index].Transition.pResource = depthTextures[index].Get();
        initialBarriers[index].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        initialBarriers[index].Transition.StateBefore = inputPaths
            ? D3D12_RESOURCE_STATE_COPY_DEST
            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        initialBarriers[index].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    initialBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    initialBarriers[2].Transition.pResource = nodes.Get();
    initialBarriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    initialBarriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    initialBarriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commands->ResourceBarrier(static_cast<UINT>(initialBarriers.size()), initialBarriers.data());

    D3D12_SHADER_RESOURCE_VIEW_DESC textureSrv{};
    textureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textureSrv.Format = DXGI_FORMAT_R32_FLOAT;
    textureSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depthTextures[0].Get(), &textureSrv, cpuAt(0));
    device->CreateShaderResourceView(depthTextures[1].Get(), &textureSrv, cpuAt(1));

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    readbackHeap.CreationNodeMask = 1;
    readbackHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC readbackDescription{};
    readbackDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDescription.Width = outputCapacity;
    readbackDescription.Height = 1;
    readbackDescription.DepthOrArraySize = 1;
    readbackDescription.MipLevels = 1;
    readbackDescription.SampleDesc.Count = 1;
    readbackDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    check(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE,
                                         &readbackDescription,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(&readback)),
          "create full-tile readback");

    ID3D12DescriptorHeap *descriptorHeap[] = {descriptors.Get()};
    commands->SetDescriptorHeaps(1, descriptorHeap);
    commands->SetComputeRootSignature(root);
    commands->SetComputeRootDescriptorTable(1, gpuAt(0));
    commands->SetComputeRootDescriptorTable(2, gpuAt(4));
    for (size_t index = 0; index != stages.size(); ++index)
    {
        const auto &stage = stages[index];
        ID3D12PipelineState *pipeline = nullptr;
        switch (stage.kind)
        {
        case StageKind::process:
            pipeline = processPipeline;
            break;
        case StageKind::prepare:
            pipeline = preparePipeline;
            break;
        case StageKind::serialize:
            pipeline = serializePipeline;
            break;
        case StageKind::reset:
            pipeline = resetPipeline;
            break;
        }
        commands->SetComputeRootConstantBufferView(
            0, constants->GetGPUVirtualAddress() + index * constantStride);
        commands->SetPipelineState(pipeline);
        commands->Dispatch(stage.groups, 1, 1);
        std::array<D3D12_RESOURCE_BARRIER, 2> uavBarriers{};
        for (UINT barrier = 0; barrier != uavBarriers.size(); ++barrier)
        {
            uavBarriers[barrier].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarriers[barrier].UAV.pResource = barrier == 0 ? nodes.Get() : rawOutput.Get();
        }
        commands->ResourceBarrier(static_cast<UINT>(uavBarriers.size()), uavBarriers.data());
    }

    D3D12_RESOURCE_BARRIER outputBarrier{};
    outputBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    outputBarrier.Transition.pResource = rawOutput.Get();
    outputBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    outputBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    outputBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &outputBarrier);
    commands->CopyBufferRegion(readback.Get(), 0, rawOutput.Get(), 0, outputCapacity);
    check(commands->Close(), "close full-tile command list");
    ID3D12CommandList *lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    waitForGpu(queue.Get(), device);

    std::vector<std::uint8_t> bytes(outputCapacity);
    D3D12_RANGE readRange{0, outputCapacity};
    check(readback->Map(0, &readRange, &mapped), "map full-tile readback");
    std::memcpy(bytes.data(), mapped, bytes.size());
    D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    std::uint32_t serializedSize = 0;
    std::memcpy(&serializedSize, bytes.data(), sizeof(serializedSize));
    if (serializedSize < 80 || serializedSize > bytes.size())
    {
        std::ofstream diagnostic(outputPath, std::ios::binary);
        if (diagnostic)
            diagnostic.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        std::string header;
        for (size_t word = 0; word != 8; ++word)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, bytes.data() + word * sizeof(value), sizeof(value));
            header += (word ? " " : "") + std::to_string(value);
        }
        throw std::runtime_error("serializer wrote invalid mini-tile size " +
                                 std::to_string(serializedSize) + "; header=" + header +
                                 "; raw diagnostic at " + outputPath.string());
    }
    const auto wordAt = [&](const size_t offset)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    if ((depthMode != 3 && depthMode != 10 && depthMode != 11 && serializedSize !=
             (depthMode == 1 || depthMode == 2 || depthMode >= 8 ? 96u : 80u)) ||
        wordAt(4) != depthResolution ||
        wordAt(8) != std::bit_cast<std::uint32_t>(1.0f / resolution) ||
        wordAt(12) != serializedSize ||
        wordAt(16) != 0)
        throw std::runtime_error("Replay shader wrote unexpected mini-tile layout: " +
                                 std::to_string(serializedSize) + "/" +
                                 std::to_string(wordAt(4)) + "/" +
                                 std::to_string(wordAt(8)) + "/" +
                                 std::to_string(wordAt(12)) + "/" +
                                 std::to_string(wordAt(16)));
    if (depthMode < 3)
    {
        const std::array<std::uint32_t, 4> expectedRoots = depthMode
            ? std::array<std::uint32_t, 4>{0x30C0, 0x2830, 0x200C, 0x1803}
            : std::array<std::uint32_t, 4>{0x3000, 0x2400, 0x1800, 0x0C00};
        for (size_t index = 0; index != expectedRoots.size(); ++index)
            if (wordAt(32 + 12 * index) != expectedRoots[index])
                throw std::runtime_error("Replay shader wrote unexpected root layout");
    }
    if (depthMode >= 4 && depthMode < 8)
    {
        constexpr std::array<std::uint32_t, 4> expectedRoots{
            0x3000, 0x2400, 0x1800, 0x0C00};
        for (size_t index = 0; index != expectedRoots.size(); ++index)
        {
            const float expectedDepth = index == depthMode - 4 ? 0.25f : 0.5f;
            if (wordAt(32 + 12 * index) != expectedRoots[index] ||
                std::abs(std::bit_cast<float>(wordAt(36 + 12 * index)) -
                         expectedDepth) > 1e-5f ||
                std::abs(std::bit_cast<float>(wordAt(40 + 12 * index)) -
                         expectedDepth) > 1e-5f)
                throw std::runtime_error("Replay shader quadrant/root order changed");
        }
    }
    if (depthMode == 8 || depthMode == 9)
    {
        constexpr std::array<std::uint32_t, 4> expectedRoots{
            0x30C0, 0x2830, 0x200C, 0x1803};
        for (size_t index = 0; index != expectedRoots.size(); ++index)
        {
            const float backgroundDepth = depthMode == 8 ? 1.0f : 0.0f;
            if (wordAt(32 + 12 * index) != expectedRoots[index] ||
                std::bit_cast<float>(wordAt(36 + 12 * index)) != backgroundDepth ||
                std::bit_cast<float>(wordAt(40 + 12 * index)) != backgroundDepth ||
                std::abs(std::bit_cast<float>(wordAt(80 + 4 * index)) - 0.3f) > 1e-5f)
                throw std::runtime_error("Replay shader sparse depth contract changed");
        }
    }
    std::ofstream output(outputPath, std::ios::binary);
    if (!output || !output.write(reinterpret_cast<const char *>(bytes.data()), serializedSize))
        throw std::runtime_error("cannot write full-tile output " + outputPath.string());
    std::cout << "WARP full-tile depth="
              << (depthMode == 11 ? "input-depth-pair-2x" :
                  depthMode == 10 ? "input-depth-pair" :
                  depthMode == 9 ? "sparse-zero-clear" :
                  depthMode == 8 ? "sparse-front-back" :
                  depthMode >= 4 ? "quadrant-" + std::to_string(depthMode - 4) :
                  depthMode == 3 ? "nested" : depthMode == 2 ? "center-shell"
                 : depthMode ? "center-step" : "uniform")
              << ", stages=" << stages.size() << " (8 process, 9 serialize, "
              << "8 prepare, 8 reset), nodes=" << scratchNodeCount
              << ", raw bytes=" << serializedSize << ", output=" << outputPath.string()
              << "\n";
}
// Run the original target shaders, not a translated approximation of their
// tree walk. The fixture fixes unrelated cascade/contact-shadow inputs so the
// prepass reuse contract can be compared with a known 512x512 depth image.
void dispatchConsumers(ID3D12Device *device, const std::filesystem::path &shaderDirectory,
                       const std::filesystem::path &forestPath,
                       const std::filesystem::path &referencePath,
                       const std::filesystem::path &outputDirectory)
{
    const auto forest = readShader(forestPath);
    const auto word = [&](const size_t offset) {
        uint32_t value{};
        if (offset + sizeof(value) > forest.size())
            throw std::runtime_error("short consumer fixture");
        std::memcpy(&value, forest.data() + offset, sizeof(value));
        return value;
    };
    const UINT logicalResolution = word(4);
    const UINT forestWidth = word(20) & 0xffff, forestHeight = word(20) >> 16;
    if (forest.size() < 36 || word(0) != forest.size() || logicalResolution < 512 ||
        logicalResolution > 4096 || !std::has_single_bit(logicalResolution) ||
        word(16) != 0x30009 || !forestWidth || !forestHeight ||
        forestWidth > logicalResolution / 512 || forestHeight > logicalResolution / 512 ||
        word(24) != 0 || word(28) != std::bit_cast<uint32_t>(1.0f))
        throw std::runtime_error("consumer mode requires an identity-depth exact forest fixture");
    for (UINT cell = 0; cell < forestWidth * forestHeight; ++cell)
    {
        const UINT entry = word(32 + 4 * cell);
        if (!entry)
            continue;
        const UINT start = entry & 0x7fffffffu;
        if (!(entry & 0x80000000u) || start < 32 + 4 * forestWidth * forestHeight ||
            start > forest.size() || forest.size() - start < 80 ||
            word(start + 24) != 0 || word(start + 28) != std::bit_cast<uint32_t>(1.0f))
            throw std::runtime_error("consumer fixture has an invalid exact-depth tile");
    }
    const auto reference = readDepthImage(referencePath, logicalResolution);
    std::filesystem::create_directories(outputDirectory);

    const std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{{
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 14, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 0},
    }};
    std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
    for (unsigned index = 0; index < 3; ++index)
    {
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[index].Descriptor.ShaderRegister = index == 2 ? 7 : index;
    }
    for (unsigned index = 0; index < ranges.size(); ++index)
    {
        parameters[index + 3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[index + 3].DescriptorTable = {1, &ranges[index]};
    }
    std::array<D3D12_STATIC_SAMPLER_DESC, 4> samplers{};
    for (unsigned index = 0; index < samplers.size(); ++index)
    {
        auto &sampler = samplers[index];
        sampler.Filter = index == 0 ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT
                                    : D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = std::array<unsigned, 4>{4, 5, 7, 14}[index];
    }
    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = static_cast<UINT>(parameters.size());
    rootDescription.pParameters = parameters.data();
    rootDescription.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDescription.pStaticSamplers = samplers.data();
    ComPtr<ID3DBlob> rootBytes, rootErrors;
    check(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
                                     &rootBytes, &rootErrors), "serialize consumer root");
    ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0, rootBytes->GetBufferPointer(),
                                      rootBytes->GetBufferSize(), IID_PPV_ARGS(&root)),
          "create consumer root");
    std::array<ComPtr<ID3D12PipelineState>, 2> pipelines;
    const std::array<std::filesystem::path, 2> shaders{
        shaderDirectory / "cs_csm_prepass.hlsl" / "main.34.cso",
        shaderDirectory / "cs_sunvis.hlsl" / "cs_sunvis.434.cso"};
    for (unsigned index = 0; index < shaders.size(); ++index)
    {
        const auto code = readShader(shaders[index]);
        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = root.Get();
        description.CS = {code.data(), code.size()};
        check(device->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipelines[index])),
              "load original CSS consumer");
    }
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)),
          "create consumer queue");
    const auto upload = [&](const void *data, const size_t size) {
        auto buffer = createBuffer(device, size, D3D12_HEAP_TYPE_UPLOAD,
                                    D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped{};
        const D3D12_RANGE noRead{0, 0};
        check(buffer->Map(0, &noRead, &mapped), "map consumer input");
        std::memcpy(mapped, data, size);
        buffer->Unmap(0, nullptr);
        return buffer;
    };
    std::array<uint32_t, 64> localConstants{};
    localConstants[1] = std::bit_cast<uint32_t>(1.0f);
    auto prepassConstants = upload(localConstants.data(), sizeof(localConstants));
    localConstants.fill(0);
    auto visibilityConstants = upload(localConstants.data(), sizeof(localConstants));

    for (const bool normalizedRange : {false, true})
    for (const UINT resolution : {64u, 128u, 512u})
    for (const float receiver : {0.0f, 0.1249f, 0.1251f, 0.4999f, 0.5001f, 0.8751f, 1.0f})
    {
        auto fixture = forest;
        const float depthOrigin = normalizedRange ? 32.0f : 0.0f;
        const float depthScale = normalizedRange ? 0.125f : 1.0f;
        if (normalizedRange)
        {
            // Deliberately different outer and mini ranges catch a consumer
            // using the forest normalization for a tagged mini-tile.
            const std::array<float, 2> outerRange{64.0f, 0.03125f};
            std::memcpy(fixture.data() + 24, outerRange.data(), sizeof(outerRange));
            for (UINT cell = 0; cell < forestWidth * forestHeight; ++cell)
                if (const UINT entry = word(32 + 4 * cell))
                {
                    const std::array<float, 2> miniRange{
                        depthOrigin + cell * 4.0f, depthScale / (cell + 1)};
                    std::memcpy(fixture.data() + (entry & 0x7fffffffu) + 24,
                                miniRange.data(), sizeof(miniRange));
                }
        }
        auto forestBuffer = upload(fixture.data(), fixture.size());
        const UINT tiles = resolution / 8;
        const UINT flagsRow = ((tiles + 7) / 8) * 2;
        const UINT flagsPlane = ((tiles + 7) / 8) * flagsRow;
        const UINT prepassBytes = tiles * tiles * 8;
        std::vector<uint32_t> zeros(prepassBytes / 4 + 4 * flagsPlane);
        auto zeroBuffer = upload(zeros.data(), zeros.size() * sizeof(uint32_t));
        std::array<std::array<float, 4>, 176> globals{};
        globals[53][0] = std::bit_cast<float>(flagsRow);
        globals[53][1] = std::bit_cast<float>(flagsPlane);
        globals[54] = {float(resolution), float(resolution),
                       1.0f / resolution, 1.0f / resolution};
        globals[65] = {1, 0, 0, 0};
        globals[66] = {0, 1, 0, 0};
        globals[67] = {0, 0, 0, receiver / depthScale - depthOrigin};
        globals[83] = {1, 0, 0, 1}; // compressed path; no view bias
        globals[92] = {-0.5f, 0, 0, 0};
        globals[93] = {0, -0.5f, 0, 0};
        globals[94] = {0.5f + 0.5f / logicalResolution,
                       0.5f + 0.5f / logicalResolution, 1, 0};
        globals[171][2] = 1;
        auto globalConstants = upload(globals.data(), sizeof(globals));
        auto depthTexture = createDepthTexture(device, resolution, 4);
        auto visibility = createDepthTexture(device, resolution);
        auto prepass = createBuffer(device, prepassBytes, D3D12_HEAP_TYPE_DEFAULT,
                                    D3D12_RESOURCE_STATE_COPY_DEST,
                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        // Two SRV tables, two UAV tables, then four depth-mip clear views.
        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDescription.NumDescriptors = 38;
        heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> descriptors;
        check(device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&descriptors)),
              "create consumer descriptors");
        const UINT stride = device->GetDescriptorHandleIncrementSize(heapDescription.Type);
        const auto cpuAt = [&](UINT index) {
            auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += SIZE_T(index) * stride;
            return handle;
        };
        const auto gpuAt = [&](UINT index) {
            auto handle = descriptors->GetGPUDescriptorHandleForHeapStart();
            handle.ptr += UINT64(index) * stride;
            return handle;
        };
        D3D12_SHADER_RESOURCE_VIEW_DESC textureSrv{};
        textureSrv.Format = DXGI_FORMAT_R32_FLOAT;
        textureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        // Both native shaders can select different lanes; this depth fixture
        // deliberately presents the same scalar in all four components.
        textureSrv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0);
        textureSrv.Texture2D.MipLevels = 4;
        for (UINT index = 0; index < 28; ++index)
            device->CreateShaderResourceView(nullptr, &textureSrv, cpuAt(index));
        for (const UINT index : {0u, 1u, 21u})
            device->CreateShaderResourceView(depthTexture.Get(), &textureSrv, cpuAt(index));
        D3D12_SHADER_RESOURCE_VIEW_DESC rawSrv{};
        rawSrv.Format = DXGI_FORMAT_R32_TYPELESS;
        rawSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        rawSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rawSrv.Buffer.NumElements = static_cast<UINT>(forest.size() / 4);
        rawSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        for (const UINT index : {6u, 23u})
            device->CreateShaderResourceView(forestBuffer.Get(), &rawSrv, cpuAt(index));
        rawSrv.Buffer.NumElements = static_cast<UINT>(zeros.size());
        device->CreateShaderResourceView(zeroBuffer.Get(), &rawSrv, cpuAt(27));
        auto flagsSrv = rawSrv;
        flagsSrv.Format = DXGI_FORMAT_R32_UINT;
        flagsSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(zeroBuffer.Get(), &flagsSrv, cpuAt(7));
        auto prepassSrv = rawSrv;
        prepassSrv.Format = DXGI_FORMAT_UNKNOWN;
        prepassSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        prepassSrv.Buffer.NumElements = tiles * tiles;
        prepassSrv.Buffer.StructureByteStride = 8;
        device->CreateShaderResourceView(prepass.Get(), &prepassSrv, cpuAt(26));
        D3D12_UNORDERED_ACCESS_VIEW_DESC rawUav{};
        rawUav.Format = DXGI_FORMAT_R32_TYPELESS;
        rawUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        rawUav.Buffer.NumElements = 4;
        rawUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        for (UINT index = 28; index < 34; ++index)
            device->CreateUnorderedAccessView(nullptr, nullptr, &rawUav, cpuAt(index));
        auto prepassUav = rawUav;
        prepassUav.Format = DXGI_FORMAT_UNKNOWN;
        prepassUav.Buffer.NumElements = tiles * tiles;
        prepassUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        prepassUav.Buffer.StructureByteStride = 8;
        device->CreateUnorderedAccessView(prepass.Get(), nullptr, &prepassUav, cpuAt(28));
        D3D12_UNORDERED_ACCESS_VIEW_DESC textureUav{};
        textureUav.Format = DXGI_FORMAT_R32_FLOAT;
        textureUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(visibility.Get(), nullptr, &textureUav, cpuAt(31));
        for (UINT mip = 0; mip < 4; ++mip)
        {
            textureUav.Texture2D.MipSlice = mip;
            device->CreateUnorderedAccessView(depthTexture.Get(), nullptr, &textureUav,
                                               cpuAt(34 + mip));
        }

        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator)), "consumer allocator");
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                        nullptr, IID_PPV_ARGS(&commands)), "consumer commands");
        const auto transition = [&](ID3D12Resource *resource, D3D12_RESOURCE_STATES from,
                                    D3D12_RESOURCE_STATES to) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
            commands->ResourceBarrier(1, &barrier);
        };
        ID3D12DescriptorHeap *heaps[]{descriptors.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        const float depthValue[4]{1.0f / 6, 1.0f / 6, 1.0f / 6, 1.0f / 6};
        for (UINT mip = 0; mip < 4; ++mip)
            commands->ClearUnorderedAccessViewFloat(gpuAt(34 + mip), cpuAt(34 + mip),
                                                     depthTexture.Get(), depthValue, 0, nullptr);
        const float uninitialized[4]{-1, -1, -1, -1};
        commands->ClearUnorderedAccessViewFloat(gpuAt(31), cpuAt(31), visibility.Get(),
                                                uninitialized, 0, nullptr);
        transition(depthTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commands->CopyBufferRegion(prepass.Get(), 0, zeroBuffer.Get(), 0, prepassBytes);
        transition(prepass.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands->SetComputeRootSignature(root.Get());
        commands->SetComputeRootConstantBufferView(0, prepassConstants->GetGPUVirtualAddress());
        commands->SetComputeRootConstantBufferView(1, visibilityConstants->GetGPUVirtualAddress());
        commands->SetComputeRootConstantBufferView(2, globalConstants->GetGPUVirtualAddress());
        commands->SetComputeRootDescriptorTable(3, gpuAt(0));
        commands->SetComputeRootDescriptorTable(4, gpuAt(28));
        commands->SetPipelineState(pipelines[0].Get());
        commands->Dispatch((tiles + 7) / 8, (tiles + 7) / 8, 1);
        transition(prepass.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commands->SetComputeRootDescriptorTable(3, gpuAt(14));
        commands->SetComputeRootDescriptorTable(4, gpuAt(31));
        commands->SetPipelineState(pipelines[1].Get());
        commands->Dispatch(tiles, tiles, 1);
        transition(visibility.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(prepass.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 imageBytes{};
        const auto textureDescription = visibility->GetDesc();
        device->GetCopyableFootprints(&textureDescription, 0, 1, 0, &footprint,
                                      nullptr, nullptr, &imageBytes);
        auto readback = createBuffer(device, imageBytes + prepassBytes,
                                     D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION destination{}, source{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        source.pResource = visibility.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        commands->CopyBufferRegion(readback.Get(), imageBytes, prepass.Get(), 0, prepassBytes);
        check(commands->Close(), "close consumer commands");
        ID3D12CommandList *lists[]{commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        waitForGpu(queue.Get(), device);
        check(device->GetDeviceRemovedReason(), "consumer device status");

        void *mapped{};
        const D3D12_RANGE readRange{0, static_cast<SIZE_T>(imageBytes + prepassBytes)};
        check(readback->Map(0, &readRange, &mapped), "map consumer output");
        std::vector<float> pixels(resolution * resolution);
        for (UINT y = 0; y < resolution; ++y)
            std::memcpy(pixels.data() + y * resolution,
                        static_cast<const uint8_t *>(mapped) + footprint.Offset +
                            y * footprint.Footprint.RowPitch,
                        resolution * sizeof(float));
        std::vector<uint32_t> prepassWords(prepassBytes / 4);
        std::memcpy(prepassWords.data(), static_cast<const uint8_t *>(mapped) + imageBytes,
                    prepassBytes);
        const D3D12_RANGE noWrite{0, 0};
        readback->Unmap(0, &noWrite);
        const std::string name = (normalizedRange ? "offset-" : "identity-") +
                                 std::to_string(resolution) + "-" + std::to_string(receiver);
        std::ofstream(outputDirectory / (name + ".visibility.r32"), std::ios::binary)
            .write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * sizeof(float));
        std::ofstream(outputDirectory / (name + ".prepass.raw"), std::ios::binary)
            .write(reinterpret_cast<const char *>(prepassWords.data()), prepassBytes);
        std::array<UINT, 16> classes{}, reuseDepths{};
        for (UINT index = 0; index < tiles * tiles; ++index)
        {
            ++classes[prepassWords[2 * index + 1] >> 28];
            ++reuseDepths[(prepassWords[2 * index + 1] >> 24) & 15];
        }
        UINT mismatches = 0, shadowed = 0, firstMismatch = 0;
        for (UINT y = 0; y < resolution; ++y)
        for (UINT x = 0; x < resolution; ++x)
        {
            const UINT sampleX = x * logicalResolution / resolution;
            const UINT sampleY = y * logicalResolution / resolution;
            const float threshold = reference[sampleY * logicalResolution + sampleX];
            // The threshold uses the mini range, but the native early-lit
            // guard uses projected depth plus the OUTER origin, unscaled.
            const float globalReceiver = globals[67][3] + (normalizedRange ? 64.0f : 0.0f);
            const UINT cell = sampleX / 512 + sampleY / 512 * forestWidth;
            // Match the separate float add/multiply fed by the uploaded
            // projection and mini header. Algebraic simplification changes
            // rounding at an exact depth equality in real map samples.
            const float miniOrigin = normalizedRange ? depthOrigin + cell * 4.0f : 0.0f;
            const float miniScale = normalizedRange ? depthScale / (cell + 1) : 1.0f;
            const float shiftedReceiver = globals[67][3] + miniOrigin;
            const float localReceiver = shiftedReceiver * miniScale;
            const float expected = threshold == 0 || globalReceiver <= 0.000015f || localReceiver >= threshold
                                       ? 1.0f : 0.0f;
            shadowed += expected == 0;
            if (!std::isfinite(pixels[y * resolution + x]) ||
                std::abs(pixels[y * resolution + x] - expected) > 0.000001f)
            {
                if (!mismatches)
                    firstMismatch = y * resolution + x;
                ++mismatches;
            }
        }
        std::cout << "native consumers " << name << ": samples=" << pixels.size()
                  << ", expected shadowed=" << shadowed << ", mismatches=" << mismatches
                  << ", class13=" << classes[13] << "/" << tiles * tiles
                  << ", reuse codes=";
        for (UINT code = 0; code < reuseDepths.size(); ++code)
            if (reuseDepths[code])
                std::cout << code << ':' << reuseDepths[code] << ' ';
        std::cout << '\n';
        if (mismatches || classes[13] != tiles * tiles)
            throw std::runtime_error("native CSS consumer differs at (" +
                                     std::to_string(firstMismatch % resolution) + "," +
                                     std::to_string(firstMismatch / resolution) +
                                     "), got=" + std::to_string(pixels[firstMismatch]));
    }
}
} // namespace

int main(int argc, char **argv)
{
    const bool processNodesMode = argc == 3 && std::string(argv[2]) == "--process-nodes";
    const bool exploreDepthMode = argc == 3 && std::string(argv[2]) == "--explore-depth";
    const bool fullTileMode = argc == 4 && std::string(argv[2]) == "--full-tile";
    const bool fullTileStepMode = argc == 4 && std::string(argv[2]) == "--full-tile-step";
    const bool fullTileShellMode = argc == 4 && std::string(argv[2]) == "--full-tile-shell";
    const bool fullTileNestedMode = argc == 4 && std::string(argv[2]) == "--full-tile-nested";
    const bool fullTileQuadrantMode = argc == 5 && std::string(argv[2]) == "--full-tile-quadrant";
    const bool fullTileSparseMode = argc == 4 && std::string(argv[2]) == "--full-tile-sparse";
    const bool fullTileSparseZeroMode = argc == 4 && std::string(argv[2]) == "--full-tile-sparse-zero";
    const bool fullTileInputMode = argc == 6 && std::string(argv[2]) == "--full-tile-input";
    const bool fullTileInput2xMode = argc == 6 && std::string(argv[2]) == "--full-tile-input-2x";
    const bool consumerMode = argc == 6 && std::string(argv[2]) == "--consume";
    if (argc < 2 || argc > 6 ||
        (argc != 2 && !processNodesMode && !exploreDepthMode && !fullTileMode && !fullTileStepMode &&
         !fullTileShellMode && !fullTileNestedMode && !fullTileQuadrantMode &&
         !fullTileSparseMode && !fullTileSparseZeroMode &&
         !fullTileInputMode && !fullTileInput2xMode && !consumerMode))
    {
        std::cerr << "usage: css-native-shaders <Replay cs_compress_shadow_map.hlsl directory> "
                     "[--process-nodes | --explore-depth | --full-tile <output.raw> | "
                     "--full-tile-step <output.raw> | --full-tile-shell <output.raw> | "
                     "--full-tile-nested <output.raw> | "
                     "--full-tile-quadrant <0..3> <output.raw> | "
                     "--full-tile-sparse <output.raw> | "
                     "--full-tile-sparse-zero <output.raw> | "
                     "--full-tile-input <t0.r32> <t1.r32> <output.raw> | "
                     "--full-tile-input-2x <t0-1024.r32> <t1-1024.r32> <output.raw>]\n"
                     "       css-native-shaders <Replay computeshader directory> "
                     "--consume <exact.css> <reference.r32> <output-directory>\n";
        return 2;
    }
    try
    {
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
        ComPtr<IDXGIAdapter> warp;
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
        ComPtr<ID3D12Device> device;
        check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(&device)), "D3D12 WARP device");
        check(device->GetDeviceRemovedReason(), "WARP device after creation");
        if (consumerMode)
        {
            dispatchConsumers(device.Get(), argv[1], argv[3], argv[4], argv[5]);
            return 0;
        }

        const std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{{
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0},
        }};
        std::array<D3D12_ROOT_PARAMETER, 3> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister = 0;
        for (unsigned index = 0; index != ranges.size(); ++index)
        {
            parameters[index + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index + 1].DescriptorTable.NumDescriptorRanges = 1;
            parameters[index + 1].DescriptorTable.pDescriptorRanges = &ranges[index];
        }
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 5;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = static_cast<unsigned>(parameters.size());
        description.pParameters = parameters.data();
        description.NumStaticSamplers = 1;
        description.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> rootBytes, error;
        check(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
                                          &rootBytes, &error), "serialize root signature");
        ComPtr<ID3D12RootSignature> root;
        check(device->CreateRootSignature(0, rootBytes->GetBufferPointer(),
                                          rootBytes->GetBufferSize(), IID_PPV_ARGS(&root)),
              "create root signature");
        check(device->GetDeviceRemovedReason(), "WARP device after root signature");

        constexpr std::array<const char *, 5> names{
            "debug_copy_depth.15.cso", "prepare_data_layout.16.cso",
            "process_nodes.17.cso", "reset_data_layout.18.cso",
            "serialize_tree.19.cso"};
        ComPtr<ID3D12PipelineState> preparePipeline;
        ComPtr<ID3D12PipelineState> processNodesPipeline;
        ComPtr<ID3D12PipelineState> resetPipeline;
        ComPtr<ID3D12PipelineState> serializePipeline;
        for (const char *name : names)
        {
            const auto shader = readShader(std::filesystem::path(argv[1]) / name);
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDescription{};
            pipelineDescription.pRootSignature = root.Get();
            pipelineDescription.CS = {shader.data(), shader.size()};
            ComPtr<ID3D12PipelineState> pipeline;
            check(device->CreateComputePipelineState(&pipelineDescription,
                                                      IID_PPV_ARGS(&pipeline)), name);
            check(device->GetDeviceRemovedReason(), "WARP device after pipeline creation");
            std::cout << "WARP loaded " << name << '\n';
            if (std::string(name) == "prepare_data_layout.16.cso")
                preparePipeline = pipeline;
            else if (std::string(name) == "process_nodes.17.cso")
                processNodesPipeline = pipeline;
            else if (std::string(name) == "reset_data_layout.18.cso")
                resetPipeline = pipeline;
            else if (std::string(name) == "serialize_tree.19.cso")
                serializePipeline = pipeline;
        }
        if (processNodesMode)
        {
            dispatchDepthPair(device.Get(), root.Get(), processNodesPipeline.Get(),
                              0.5f, 0.5f, 0.5f, 0.5f);
            dispatchDepthPair(device.Get(), root.Get(), processNodesPipeline.Get(),
                              0.25f, 0.75f, 0.75f, 0.755f);
            dispatchDepthPair(device.Get(), root.Get(), processNodesPipeline.Get(),
                              0.75f, 0.25f, 0.25f, 0.75f);
        }
        else if (exploreDepthMode)
        {
            const float unknown = std::numeric_limits<float>::quiet_NaN();
            for (const float t0 : {0.0f, 0.5f, 1.0f})
                for (const float t1 : {0.0f, 0.5f, 1.0f})
                    dispatchDepthPair(device.Get(), root.Get(), processNodesPipeline.Get(),
                                      t0, t1, unknown, unknown);
        }
        else if (fullTileMode || fullTileStepMode || fullTileShellMode ||
                 fullTileNestedMode || fullTileQuadrantMode || fullTileSparseMode ||
                 fullTileSparseZeroMode || fullTileInputMode || fullTileInput2xMode)
        {
            unsigned quadrant = 0;
            if (fullTileQuadrantMode)
            {
                const std::string number = argv[3];
                if (number.size() != 1 || number[0] < '0' || number[0] > '3')
                    throw std::runtime_error("quadrant must be 0..3");
                quadrant = static_cast<unsigned>(number[0] - '0');
            }
            const std::array<std::filesystem::path, 2> inputPaths =
                (fullTileInputMode || fullTileInput2xMode)
                    ? std::array<std::filesystem::path, 2>{argv[3], argv[4]}
                    : std::array<std::filesystem::path, 2>{};
            dispatchFullTile(device.Get(), root.Get(), preparePipeline.Get(),
                             processNodesPipeline.Get(), resetPipeline.Get(),
                             serializePipeline.Get(), (fullTileInputMode || fullTileInput2xMode) ? argv[5] :
                             fullTileQuadrantMode ? argv[4] : argv[3],
                             fullTileInput2xMode ? 11u : fullTileInputMode ? 10u
                             : fullTileSparseZeroMode ? 9u
                             : fullTileSparseMode ? 8u
                             : fullTileQuadrantMode ? 4u + quadrant
                             : fullTileNestedMode ? 3u
                             : fullTileShellMode ? 2u : fullTileStepMode ? 1u : 0u,
                             (fullTileInputMode || fullTileInput2xMode) ? &inputPaths : nullptr);
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
