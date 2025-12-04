// OSFG - Open Source Frame Generation
// Inter-GPU Transfer Engine Implementation

#include "gpu_transfer.h"
#include <sstream>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace osfg {

GPUTransfer::GPUTransfer() = default;

GPUTransfer::~GPUTransfer() {
    Shutdown();
}

std::vector<GPUInfo> GPUTransfer::EnumerateGPUs() {
    std::vector<GPUInfo> gpus;

    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return gpus;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        GPUInfo info;
        info.adapterIndex = i;
        info.description = desc.Description;
        info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
        info.sharedSystemMemory = desc.SharedSystemMemory;
        info.luid = desc.AdapterLuid;
        info.isIntegrated = (desc.DedicatedVideoMemory < 512 * 1024 * 1024); // Heuristic

        // Check cross-adapter row-major support
        ComPtr<ID3D12Device> tempDevice;
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tempDevice));
        if (SUCCEEDED(hr)) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
            hr = tempDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
            info.supportsCrossAdapterRowMajor = SUCCEEDED(hr) && options.CrossAdapterRowMajorTextureSupported;
        }

        gpus.push_back(info);
    }

    return gpus;
}

bool GPUTransfer::IsPeerToPeerAvailable(uint32_t sourceAdapter, uint32_t destAdapter) {
    if (sourceAdapter == destAdapter) {
        return false; // Same GPU
    }

    auto gpus = EnumerateGPUs();
    if (sourceAdapter >= gpus.size() || destAdapter >= gpus.size()) {
        return false;
    }

    // Cross-adapter row-major texture support is required for efficient P2P
    return gpus[sourceAdapter].supportsCrossAdapterRowMajor &&
           gpus[destAdapter].supportsCrossAdapterRowMajor;
}

bool GPUTransfer::Initialize(const TransferConfig& config) {
    if (m_initialized) {
        Shutdown();
    }

    m_config = config;

    // Validate configuration
    if (config.sourceAdapterIndex == config.destAdapterIndex) {
        SetError("Source and destination adapters must be different");
        return false;
    }

    // Create D3D12 devices for both GPUs
    if (!CreateDevices()) {
        Shutdown();
        return false;
    }

    // Determine transfer method
    bool crossAdapterSupported = IsPeerToPeerAvailable(config.sourceAdapterIndex, config.destAdapterIndex);

    if (config.preferPeerToPeer && crossAdapterSupported) {
        m_transferMethod = TransferMethod::CrossAdapterHeap;
        if (!CreateCrossAdapterResources()) {
            if (config.allowCPUFallback) {
                m_transferMethod = TransferMethod::StagedCPU;
                if (!CreateStagingResources()) {
                    Shutdown();
                    return false;
                }
            } else {
                Shutdown();
                return false;
            }
        }
    } else if (config.allowCPUFallback) {
        m_transferMethod = TransferMethod::StagedCPU;
        if (!CreateStagingResources()) {
            Shutdown();
            return false;
        }
    } else {
        SetError("No suitable transfer method available");
        Shutdown();
        return false;
    }

    // Create synchronization objects
    if (!CreateSyncObjects()) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    ResetStats();
    return true;
}

void GPUTransfer::Shutdown() {
    // Wait for pending work
    if (m_sourceFenceEvent) {
        WaitForTransfer();
    }

    // Close handles
    if (m_sourceFenceEvent) {
        CloseHandle(m_sourceFenceEvent);
        m_sourceFenceEvent = nullptr;
    }
    if (m_destFenceEvent) {
        CloseHandle(m_destFenceEvent);
        m_destFenceEvent = nullptr;
    }
    if (m_sharedFenceHandle) {
        CloseHandle(m_sharedFenceHandle);
        m_sharedFenceHandle = nullptr;
    }

    // Release resources
    m_crossAdapterTextures.clear();
    m_destTextures.clear();
    m_crossAdapterHeap.Reset();
    m_sourceReadbackBuffer.Reset();
    m_destUploadBuffer.Reset();

    m_sharedFence.Reset();
    m_sourceFence.Reset();
    m_destFence.Reset();

    m_sourceCommandList.Reset();
    m_sourceCommandAllocator.Reset();
    m_sourceCommandQueue.Reset();
    m_sourceDevice.Reset();

    m_destCommandList.Reset();
    m_destCommandAllocator.Reset();
    m_destCommandQueue.Reset();
    m_destDevice.Reset();

    m_stagingMemory = nullptr;
    m_stagingSize = 0;
    m_initialized = false;
    m_transferMethod = TransferMethod::Unknown;
}

bool GPUTransfer::CreateDevices() {
    HRESULT hr;

    ComPtr<IDXGIFactory6> factory;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        SetError("Failed to create DXGI factory");
        return false;
    }

    // Create source GPU device
    ComPtr<IDXGIAdapter1> sourceAdapter;
    hr = factory->EnumAdapters1(m_config.sourceAdapterIndex, &sourceAdapter);
    if (FAILED(hr)) {
        SetError("Failed to get source adapter");
        return false;
    }

    hr = D3D12CreateDevice(sourceAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_sourceDevice));
    if (FAILED(hr)) {
        SetError("Failed to create source D3D12 device");
        return false;
    }

    // Create source command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = m_sourceDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_sourceCommandQueue));
    if (FAILED(hr)) {
        SetError("Failed to create source command queue");
        return false;
    }

    // Create destination GPU device
    ComPtr<IDXGIAdapter1> destAdapter;
    hr = factory->EnumAdapters1(m_config.destAdapterIndex, &destAdapter);
    if (FAILED(hr)) {
        SetError("Failed to get destination adapter");
        return false;
    }

    hr = D3D12CreateDevice(destAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_destDevice));
    if (FAILED(hr)) {
        SetError("Failed to create destination D3D12 device");
        return false;
    }

    // Create destination command queue
    hr = m_destDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_destCommandQueue));
    if (FAILED(hr)) {
        SetError("Failed to create destination command queue");
        return false;
    }

    // Create command allocators and lists
    hr = m_sourceDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_sourceCommandAllocator));
    if (FAILED(hr)) {
        SetError("Failed to create source command allocator");
        return false;
    }

    hr = m_sourceDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_sourceCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_sourceCommandList));
    if (FAILED(hr)) {
        SetError("Failed to create source command list");
        return false;
    }
    m_sourceCommandList->Close();

    hr = m_destDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_destCommandAllocator));
    if (FAILED(hr)) {
        SetError("Failed to create destination command allocator");
        return false;
    }

    hr = m_destDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_destCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_destCommandList));
    if (FAILED(hr)) {
        SetError("Failed to create destination command list");
        return false;
    }
    m_destCommandList->Close();

    return true;
}

bool GPUTransfer::CreateCrossAdapterResources() {
    HRESULT hr;

    // Calculate texture size
    UINT64 textureSize = 0;
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = m_config.width;
    textureDesc.Height = m_config.height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = m_config.format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // Required for cross-adapter
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    D3D12_RESOURCE_ALLOCATION_INFO allocInfo = m_sourceDevice->GetResourceAllocationInfo(0, 1, &textureDesc);
    textureSize = allocInfo.SizeInBytes;

    // Create cross-adapter heap on source GPU
    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = textureSize * m_config.bufferCount;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapDesc.Flags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;

    hr = m_sourceDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(&m_crossAdapterHeap));
    if (FAILED(hr)) {
        SetError("Failed to create cross-adapter heap");
        return false;
    }

    // Create textures on source GPU using the shared heap
    m_crossAdapterTextures.resize(m_config.bufferCount);
    for (uint32_t i = 0; i < m_config.bufferCount; i++) {
        hr = m_sourceDevice->CreatePlacedResource(
            m_crossAdapterHeap.Get(),
            i * textureSize,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_crossAdapterTextures[i]));

        if (FAILED(hr)) {
            SetError("Failed to create cross-adapter texture " + std::to_string(i));
            return false;
        }
    }

    // Open the shared heap on the destination GPU
    HANDLE heapHandle = nullptr;
    hr = m_sourceDevice->CreateSharedHandle(m_crossAdapterHeap.Get(), nullptr, GENERIC_ALL, nullptr, &heapHandle);
    if (FAILED(hr)) {
        SetError("Failed to create shared heap handle");
        return false;
    }

    ComPtr<ID3D12Heap> destHeap;
    hr = m_destDevice->OpenSharedHandle(heapHandle, IID_PPV_ARGS(&destHeap));
    CloseHandle(heapHandle);
    if (FAILED(hr)) {
        SetError("Failed to open shared heap on destination GPU");
        return false;
    }

    // Create destination textures using the shared heap
    m_destTextures.resize(m_config.bufferCount);
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE; // Destination doesn't need cross-adapter flag

    for (uint32_t i = 0; i < m_config.bufferCount; i++) {
        hr = m_destDevice->CreatePlacedResource(
            destHeap.Get(),
            i * textureSize,
            &textureDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&m_destTextures[i]));

        if (FAILED(hr)) {
            SetError("Failed to create destination texture " + std::to_string(i));
            return false;
        }
    }

    return true;
}

bool GPUTransfer::CreateStagingResources() {
    HRESULT hr;

    // Calculate buffer size (aligned row pitch)
    UINT rowPitch = (m_config.width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                    ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    m_stagingSize = rowPitch * m_config.height;

    // Create readback buffer on source GPU
    D3D12_HEAP_PROPERTIES readbackHeapProps = {};
    readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = m_stagingSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = m_sourceDevice->CreateCommittedResource(
        &readbackHeapProps, D3D12_HEAP_FLAG_NONE,
        &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&m_sourceReadbackBuffer));
    if (FAILED(hr)) {
        SetError("Failed to create source readback buffer");
        return false;
    }

    // Create upload buffer on destination GPU
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    hr = m_destDevice->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&m_destUploadBuffer));
    if (FAILED(hr)) {
        SetError("Failed to create destination upload buffer");
        return false;
    }

    // Create destination textures
    m_destTextures.resize(m_config.bufferCount);

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = m_config.width;
    textureDesc.Height = m_config.height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = m_config.format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    for (uint32_t i = 0; i < m_config.bufferCount; i++) {
        hr = m_destDevice->CreateCommittedResource(
            &defaultHeapProps, D3D12_HEAP_FLAG_NONE,
            &textureDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&m_destTextures[i]));

        if (FAILED(hr)) {
            SetError("Failed to create destination texture " + std::to_string(i));
            return false;
        }
    }

    return true;
}

bool GPUTransfer::CreateSyncObjects() {
    HRESULT hr;

    // Create source fence
    hr = m_sourceDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_sourceFence));
    if (FAILED(hr)) {
        SetError("Failed to create source fence");
        return false;
    }

    m_sourceFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_sourceFenceEvent) {
        SetError("Failed to create source fence event");
        return false;
    }

    // Create destination fence
    hr = m_destDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_destFence));
    if (FAILED(hr)) {
        SetError("Failed to create destination fence");
        return false;
    }

    m_destFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_destFenceEvent) {
        SetError("Failed to create destination fence event");
        return false;
    }

    // Create shared fence for cross-adapter synchronization
    if (m_transferMethod == TransferMethod::CrossAdapterHeap) {
        hr = m_sourceDevice->CreateFence(0, D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER,
            IID_PPV_ARGS(&m_sharedFence));
        if (FAILED(hr)) {
            SetError("Failed to create shared fence");
            return false;
        }

        hr = m_sourceDevice->CreateSharedHandle(m_sharedFence.Get(), nullptr, GENERIC_ALL, nullptr, &m_sharedFenceHandle);
        if (FAILED(hr)) {
            SetError("Failed to create shared fence handle");
            return false;
        }
    }

    return true;
}

bool GPUTransfer::TransferFrame(ID3D12Resource* sourceTexture) {
    if (!m_initialized) {
        SetError("Not initialized");
        return false;
    }

    if (!sourceTexture) {
        SetError("Source texture is null");
        return false;
    }

    m_transferStart = std::chrono::high_resolution_clock::now();

    bool success = false;
    if (m_transferMethod == TransferMethod::CrossAdapterHeap) {
        success = TransferViaCrossAdapter(sourceTexture);
    } else {
        success = TransferViaStaging(sourceTexture);
    }

    if (success) {
        auto endTime = std::chrono::high_resolution_clock::now();
        double transferTimeMs = std::chrono::duration<double, std::milli>(endTime - m_transferStart).count();

        // Update statistics
        m_stats.framesTransferred++;
        m_stats.bytesTranferred += m_config.width * m_config.height * 4;
        m_stats.lastTransferTimeMs = transferTimeMs;
        m_stats.minTransferTimeMs = (std::min)(m_stats.minTransferTimeMs, transferTimeMs);
        m_stats.maxTransferTimeMs = (std::max)(m_stats.maxTransferTimeMs, transferTimeMs);

        // Running average
        double alpha = 0.1;
        m_stats.avgTransferTimeMs = m_stats.avgTransferTimeMs * (1.0 - alpha) + transferTimeMs * alpha;
        m_stats.throughputMBps = (m_config.width * m_config.height * 4) / (transferTimeMs * 1000.0);
        m_stats.currentMethod = m_transferMethod;
    }

    return success;
}

bool GPUTransfer::TransferViaCrossAdapter(ID3D12Resource* sourceTexture) {
    HRESULT hr;

    // Reset command allocator and list
    hr = m_sourceCommandAllocator->Reset();
    if (FAILED(hr)) return false;

    hr = m_sourceCommandList->Reset(m_sourceCommandAllocator.Get(), nullptr);
    if (FAILED(hr)) return false;

    // Transition cross-adapter texture to COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_crossAdapterTextures[m_currentBuffer].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_sourceCommandList->ResourceBarrier(1, &barrier);

    // Copy source texture to cross-adapter texture
    m_sourceCommandList->CopyResource(m_crossAdapterTextures[m_currentBuffer].Get(), sourceTexture);

    // Transition back to COMMON for cross-adapter access
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    m_sourceCommandList->ResourceBarrier(1, &barrier);

    hr = m_sourceCommandList->Close();
    if (FAILED(hr)) return false;

    // Execute on source GPU
    ID3D12CommandList* cmdLists[] = { m_sourceCommandList.Get() };
    m_sourceCommandQueue->ExecuteCommandLists(1, cmdLists);

    // Signal shared fence
    m_sourceFenceValue++;
    m_sourceCommandQueue->Signal(m_sharedFence.Get(), m_sourceFenceValue);

    // Wait on destination GPU for the shared fence
    m_destCommandQueue->Wait(m_sharedFence.Get(), m_sourceFenceValue);

    return true;
}

bool GPUTransfer::TransferViaStaging(ID3D12Resource* sourceTexture) {
    HRESULT hr;

    // === Source GPU: Copy texture to readback buffer ===
    hr = m_sourceCommandAllocator->Reset();
    if (FAILED(hr)) return false;

    hr = m_sourceCommandList->Reset(m_sourceCommandAllocator.Get(), nullptr);
    if (FAILED(hr)) return false;

    // Copy texture to readback buffer
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = sourceTexture;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    UINT rowPitch = (m_config.width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                    ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = m_sourceReadbackBuffer.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint.Offset = 0;
    dstLoc.PlacedFootprint.Footprint.Format = m_config.format;
    dstLoc.PlacedFootprint.Footprint.Width = m_config.width;
    dstLoc.PlacedFootprint.Footprint.Height = m_config.height;
    dstLoc.PlacedFootprint.Footprint.Depth = 1;
    dstLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

    m_sourceCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    hr = m_sourceCommandList->Close();
    if (FAILED(hr)) return false;

    ID3D12CommandList* cmdLists[] = { m_sourceCommandList.Get() };
    m_sourceCommandQueue->ExecuteCommandLists(1, cmdLists);

    // Wait for copy to complete
    m_sourceFenceValue++;
    m_sourceCommandQueue->Signal(m_sourceFence.Get(), m_sourceFenceValue);
    if (m_sourceFence->GetCompletedValue() < m_sourceFenceValue) {
        m_sourceFence->SetEventOnCompletion(m_sourceFenceValue, m_sourceFenceEvent);
        WaitForSingleObject(m_sourceFenceEvent, INFINITE);
    }

    // === CPU: Copy from readback to upload buffer ===
    void* srcData = nullptr;
    D3D12_RANGE readRange = { 0, m_stagingSize };
    hr = m_sourceReadbackBuffer->Map(0, &readRange, &srcData);
    if (FAILED(hr)) return false;

    void* dstData = nullptr;
    D3D12_RANGE writeRange = { 0, 0 };
    hr = m_destUploadBuffer->Map(0, &writeRange, &dstData);
    if (FAILED(hr)) {
        m_sourceReadbackBuffer->Unmap(0, nullptr);
        return false;
    }

    memcpy(dstData, srcData, m_stagingSize);

    D3D12_RANGE unmapRange = { 0, m_stagingSize };
    m_destUploadBuffer->Unmap(0, &unmapRange);
    m_sourceReadbackBuffer->Unmap(0, nullptr);

    // === Destination GPU: Copy upload buffer to texture ===
    hr = m_destCommandAllocator->Reset();
    if (FAILED(hr)) return false;

    hr = m_destCommandList->Reset(m_destCommandAllocator.Get(), nullptr);
    if (FAILED(hr)) return false;

    // Transition destination texture to COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_destTextures[m_currentBuffer].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_destCommandList->ResourceBarrier(1, &barrier);

    // Copy from upload buffer to texture
    srcLoc.pResource = m_destUploadBuffer.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = dstLoc.PlacedFootprint;

    dstLoc.pResource = m_destTextures[m_currentBuffer].Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    m_destCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition back to PIXEL_SHADER_RESOURCE
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_destCommandList->ResourceBarrier(1, &barrier);

    hr = m_destCommandList->Close();
    if (FAILED(hr)) return false;

    cmdLists[0] = m_destCommandList.Get();
    m_destCommandQueue->ExecuteCommandLists(1, cmdLists);

    // Signal fence
    m_destFenceValue++;
    m_destCommandQueue->Signal(m_destFence.Get(), m_destFenceValue);

    return true;
}

ID3D12Resource* GPUTransfer::GetDestinationTexture() const {
    if (!m_initialized || m_destTextures.empty()) {
        return nullptr;
    }
    return m_destTextures[m_currentBuffer].Get();
}

ID3D12Resource* GPUTransfer::GetPreviousTexture() const {
    if (!m_initialized || m_destTextures.empty()) {
        return nullptr;
    }
    return m_destTextures[m_previousBuffer].Get();
}

void GPUTransfer::AdvanceBuffer() {
    m_previousBuffer = m_currentBuffer;
    m_currentBuffer = (m_currentBuffer + 1) % m_config.bufferCount;
}

void GPUTransfer::WaitForTransfer() {
    if (m_destFence && m_destFence->GetCompletedValue() < m_destFenceValue) {
        m_destFence->SetEventOnCompletion(m_destFenceValue, m_destFenceEvent);
        WaitForSingleObject(m_destFenceEvent, INFINITE);
    }
}

void GPUTransfer::ResetStats() {
    m_stats = TransferStats{};
    m_stats.currentMethod = m_transferMethod;
}

void GPUTransfer::SetError(const std::string& error) {
    m_lastError = error;
}

} // namespace osfg
