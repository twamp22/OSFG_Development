// OSFG D3D11-D3D12 Interop Implementation
// Handles texture sharing between D3D11 (capture) and D3D12 (compute)
// MIT License - Part of Open Source Frame Generation project

#include "d3d11_d3d12_interop.h"
#include <iostream>

namespace OSFG {

D3D11D3D12Interop::D3D11D3D12Interop() = default;

D3D11D3D12Interop::~D3D11D3D12Interop()
{
    Shutdown();
}

bool D3D11D3D12Interop::Initialize(ID3D12Device* d3d12Device,
                                    ID3D12CommandQueue* commandQueue,
                                    const InteropConfig& config)
{
    if (m_initialized) {
        m_lastError = "Already initialized";
        return false;
    }

    if (!d3d12Device || !commandQueue) {
        m_lastError = "Invalid D3D12 device or command queue";
        return false;
    }

    m_d3d12Device = d3d12Device;
    m_d3d12CommandQueue = commandQueue;
    m_config = config;

    // Create D3D11On12 device
    if (!CreateD3D11On12Device()) {
        return false;
    }

    // Create shared textures
    if (!CreateSharedTextures()) {
        return false;
    }

    m_initialized = true;
    m_currentIndex = 0;
    m_frameCount = 0;

    return true;
}

void D3D11D3D12Interop::Shutdown()
{
    if (!m_initialized) return;

    // Flush any pending work
    if (m_d3d11Context) {
        m_d3d11Context->Flush();
    }

    // Release wrapped resources first
    if (m_d3d11On12Device) {
        for (int i = 0; i < 2; i++) {
            if (m_d3d11WrappedTextures[i]) {
                ID3D11Resource* resources[] = { m_d3d11WrappedTextures[i].Get() };
                m_d3d11On12Device->ReleaseWrappedResources(resources, 1);
            }
        }
    }

    // Release resources in order
    for (int i = 0; i < 2; i++) {
        m_d3d11WrappedTextures[i].Reset();
        m_d3d12Textures[i].Reset();
    }

    // Release cached staging texture
    m_cachedStagingTexture.Reset();
    m_cachedStagingDevice.Reset();

    // Release copy resources
    if (m_copyFenceEvent) {
        CloseHandle(m_copyFenceEvent);
        m_copyFenceEvent = nullptr;
    }
    m_copyFence.Reset();
    m_copyCommandList.Reset();
    m_copyCommandAllocator.Reset();

    // Unmap upload buffer before releasing
    if (m_uploadBuffer && m_uploadBufferPtr) {
        m_uploadBuffer->Unmap(0, nullptr);
        m_uploadBufferPtr = nullptr;
    }
    m_uploadBuffer.Reset();

    m_d3d11On12Device.Reset();
    m_d3d11Context.Reset();
    m_d3d11Device.Reset();
    m_d3d12CommandQueue.Reset();
    m_d3d12Device.Reset();

    m_initialized = false;
}

bool D3D11D3D12Interop::CreateD3D11On12Device()
{
    // Create D3D11 device on top of D3D12
    UINT d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Queues that D3D11On12 will synchronize with
    IUnknown* queues[] = { m_d3d12CommandQueue.Get() };

    Microsoft::WRL::ComPtr<ID3D11Device> d3d11Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11Context;

    HRESULT hr = D3D11On12CreateDevice(
        m_d3d12Device.Get(),
        d3d11DeviceFlags,
        nullptr, 0,  // Feature levels (use default)
        queues, 1,   // Command queues
        0,           // Node mask
        &d3d11Device,
        &d3d11Context,
        nullptr      // Feature level achieved
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create D3D11On12 device: 0x" + std::to_string(hr);
        return false;
    }

    m_d3d11Device = d3d11Device;
    m_d3d11Context = d3d11Context;

    // Get D3D11On12 interface
    hr = m_d3d11Device.As(&m_d3d11On12Device);
    if (FAILED(hr)) {
        m_lastError = "Failed to get ID3D11On12Device interface";
        return false;
    }

    return true;
}

bool D3D11D3D12Interop::CreateSharedTextures()
{
    // Create D3D12 textures that will be shared with D3D11
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_config.width;
    texDesc.Height = m_config.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = m_config.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    for (int i = 0; i < 2; i++) {
        // Create D3D12 texture
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_SHARED,  // Must be shared for D3D11On12
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&m_d3d12Textures[i])
        );

        if (FAILED(hr)) {
            m_lastError = "Failed to create D3D12 shared texture " + std::to_string(i);
            return false;
        }

        // Wrap for D3D11 access
        D3D11_RESOURCE_FLAGS d3d11Flags = {};
        d3d11Flags.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        hr = m_d3d11On12Device->CreateWrappedResource(
            m_d3d12Textures[i].Get(),
            &d3d11Flags,
            D3D12_RESOURCE_STATE_COPY_DEST,      // State when acquired by D3D11
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,  // State when released back
            IID_PPV_ARGS(&m_d3d11WrappedTextures[i])
        );

        if (FAILED(hr)) {
            m_lastError = "Failed to wrap D3D12 texture for D3D11: " + std::to_string(i);
            return false;
        }
    }

    // Create upload buffer for CPU staging path
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    // Calculate row pitch (must be 256-byte aligned for D3D12)
    UINT bytesPerPixel = 4;  // BGRA
    UINT rowPitch = (m_config.width * bytesPerPixel + 255) & ~255;
    UINT uploadBufferSize = rowPitch * m_config.height;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadBufferSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = m_d3d12Device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_uploadBuffer)
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create upload buffer";
        return false;
    }

    // Store the row pitch for later use
    m_uploadRowPitch = rowPitch;

    // Persistently map the upload buffer (D3D12 upload heaps can stay mapped)
    D3D12_RANGE readRange = { 0, 0 };  // We won't read from this buffer
    hr = m_uploadBuffer->Map(0, &readRange, &m_uploadBufferPtr);
    if (FAILED(hr)) {
        m_lastError = "Failed to map upload buffer";
        return false;
    }

    // Create command resources for staged copy
    hr = m_d3d12Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_copyCommandAllocator));
    if (FAILED(hr)) {
        m_lastError = "Failed to create copy command allocator";
        return false;
    }

    hr = m_d3d12Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_copyCommandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_copyCommandList));
    if (FAILED(hr)) {
        m_lastError = "Failed to create copy command list";
        return false;
    }
    m_copyCommandList->Close();

    hr = m_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence));
    if (FAILED(hr)) {
        m_lastError = "Failed to create copy fence";
        return false;
    }

    m_copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_copyFenceEvent) {
        m_lastError = "Failed to create copy fence event";
        return false;
    }

    m_copyFenceValue = 0;

    return true;
}

bool D3D11D3D12Interop::CopyFromD3D11(ID3D11Texture2D* srcTexture)
{
    if (!m_initialized) {
        m_lastError = "Not initialized";
        return false;
    }

    if (!srcTexture) {
        m_lastError = "Source texture is null";
        return false;
    }

    // Acquire the wrapped resource for D3D11 access
    ID3D11Resource* wrappedResources[] = { m_d3d11WrappedTextures[m_currentIndex].Get() };
    m_d3d11On12Device->AcquireWrappedResources(wrappedResources, 1);

    // Copy from source D3D11 texture to wrapped texture
    m_d3d11Context->CopyResource(m_d3d11WrappedTextures[m_currentIndex].Get(), srcTexture);

    // Release the wrapped resource back to D3D12
    m_d3d11On12Device->ReleaseWrappedResources(wrappedResources, 1);

    // Flush to ensure the copy is submitted
    m_d3d11Context->Flush();

    m_frameCount++;
    return true;
}

bool D3D11D3D12Interop::CopyFromD3D11Staged(ID3D11Device* srcDevice,
                                             ID3D11DeviceContext* srcContext,
                                             ID3D11Texture2D* srcTexture)
{
    if (!m_initialized) {
        m_lastError = "Not initialized";
        return false;
    }

    if (!srcDevice || !srcContext || !srcTexture) {
        m_lastError = "Invalid parameters";
        return false;
    }

    // Get texture description
    D3D11_TEXTURE2D_DESC srcDesc;
    srcTexture->GetDesc(&srcDesc);

    // Create or reuse staging texture (only recreate if device changed or doesn't exist)
    if (!m_cachedStagingTexture || m_cachedStagingDevice.Get() != srcDevice) {
        D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        HRESULT hr = srcDevice->CreateTexture2D(&stagingDesc, nullptr, &m_cachedStagingTexture);
        if (FAILED(hr)) {
            m_lastError = "Failed to create staging texture";
            return false;
        }
        m_cachedStagingDevice = srcDevice;
    }

    // Copy source to staging
    srcContext->CopyResource(m_cachedStagingTexture.Get(), srcTexture);

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = srcContext->Map(m_cachedStagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        m_lastError = "Failed to map staging texture";
        return false;
    }

    // Copy data with proper row pitch alignment to persistently mapped upload buffer
    UINT bytesPerPixel = 4;
    UINT srcRowPitch = mapped.RowPitch;

    BYTE* srcPtr = reinterpret_cast<BYTE*>(mapped.pData);
    BYTE* dstPtr = reinterpret_cast<BYTE*>(m_uploadBufferPtr);

    for (UINT y = 0; y < m_config.height; y++) {
        memcpy(dstPtr + y * m_uploadRowPitch, srcPtr + y * srcRowPitch, m_config.width * bytesPerPixel);
    }

    srcContext->Unmap(m_cachedStagingTexture.Get(), 0);

    // Now copy from upload buffer to the D3D12 texture using our internal command list
    m_copyCommandAllocator->Reset();
    m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr);

    // Transition texture to copy dest state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_d3d12Textures[m_currentIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_copyCommandList->ResourceBarrier(1, &barrier);

    // Set up texture copy location
    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = m_d3d12Textures[m_currentIndex].Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = m_uploadBuffer.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint.Offset = 0;
    srcLocation.PlacedFootprint.Footprint.Format = m_config.format;
    srcLocation.PlacedFootprint.Footprint.Width = m_config.width;
    srcLocation.PlacedFootprint.Footprint.Height = m_config.height;
    srcLocation.PlacedFootprint.Footprint.Depth = 1;
    srcLocation.PlacedFootprint.Footprint.RowPitch = m_uploadRowPitch;

    m_copyCommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    // Transition back to shader resource state
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_copyCommandList->ResourceBarrier(1, &barrier);

    // Execute the command list
    m_copyCommandList->Close();
    ID3D12CommandList* lists[] = { m_copyCommandList.Get() };
    m_d3d12CommandQueue->ExecuteCommandLists(1, lists);

    // Wait for completion
    m_copyFenceValue++;
    m_d3d12CommandQueue->Signal(m_copyFence.Get(), m_copyFenceValue);
    if (m_copyFence->GetCompletedValue() < m_copyFenceValue) {
        m_copyFence->SetEventOnCompletion(m_copyFenceValue, m_copyFenceEvent);
        WaitForSingleObject(m_copyFenceEvent, INFINITE);
    }

    m_frameCount++;
    return true;
}

void D3D11D3D12Interop::SwapBuffers()
{
    m_currentIndex = 1 - m_currentIndex;
}

} // namespace OSFG
