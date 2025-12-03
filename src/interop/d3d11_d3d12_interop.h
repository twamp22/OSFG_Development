// OSFG D3D11-D3D12 Interop
// Handles texture sharing between D3D11 (capture) and D3D12 (compute)
// MIT License - Part of Open Source Frame Generation project

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace OSFG {

// Configuration for interop
struct InteropConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    uint32_t bufferCount = 2;  // Double-buffered for previous/current frame
};

// Interop class for sharing textures between D3D11 and D3D12
class D3D11D3D12Interop {
public:
    D3D11D3D12Interop();
    ~D3D11D3D12Interop();

    // Non-copyable
    D3D11D3D12Interop(const D3D11D3D12Interop&) = delete;
    D3D11D3D12Interop& operator=(const D3D11D3D12Interop&) = delete;

    // Initialize with existing D3D12 device and command queue
    bool Initialize(ID3D12Device* d3d12Device,
                   ID3D12CommandQueue* commandQueue,
                   const InteropConfig& config);

    // Shutdown and release resources
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Copy a D3D11 texture to the current D3D12 buffer
    // This acquires the wrapped resource, copies, and releases it
    bool CopyFromD3D11(ID3D11Texture2D* srcTexture);

    // Copy from an external D3D11 device's texture via CPU staging
    // Use this when the source texture is from a different D3D11 device
    bool CopyFromD3D11Staged(ID3D11Device* srcDevice,
                              ID3D11DeviceContext* srcContext,
                              ID3D11Texture2D* srcTexture);

    // Swap buffers (current becomes previous)
    void SwapBuffers();

    // Get D3D12 textures for optical flow
    ID3D12Resource* GetCurrentFrameD3D12() const { return m_d3d12Textures[m_currentIndex].Get(); }
    ID3D12Resource* GetPreviousFrameD3D12() const { return m_d3d12Textures[1 - m_currentIndex].Get(); }

    // Get D3D11 device (created internally for interop)
    ID3D11Device* GetD3D11Device() const { return m_d3d11Device.Get(); }
    ID3D11DeviceContext* GetD3D11Context() const { return m_d3d11Context.Get(); }

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

    // Get frame count
    uint64_t GetFrameCount() const { return m_frameCount; }

private:
    bool CreateD3D11On12Device();
    bool CreateSharedTextures();

    // D3D12 objects (provided externally)
    Microsoft::WRL::ComPtr<ID3D12Device> m_d3d12Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue;

    // D3D11On12 objects
    Microsoft::WRL::ComPtr<ID3D11Device> m_d3d11Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3d11Context;
    Microsoft::WRL::ComPtr<ID3D11On12Device> m_d3d11On12Device;

    // Shared textures (D3D12 resources wrapped for D3D11 access)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_d3d12Textures[2];
    Microsoft::WRL::ComPtr<ID3D11Resource> m_d3d11WrappedTextures[2];

    // Upload resources for CPU staging path
    Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadBuffer;
    void* m_uploadBufferPtr = nullptr;  // Persistently mapped pointer
    UINT m_uploadRowPitch = 0;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_copyFence;
    HANDLE m_copyFenceEvent = nullptr;
    UINT64 m_copyFenceValue = 0;

    // State
    InteropConfig m_config;
    bool m_initialized = false;
    uint32_t m_currentIndex = 0;
    uint64_t m_frameCount = 0;
    std::string m_lastError;
};

} // namespace OSFG
