// OSFG - Open Source Frame Generation
// FidelityFX Frame Generation Wrapper
//
// Wraps the FidelityFX Frame Generation API for use with OSFG.
// This module creates and manages the FFX swap chain and frame generation context.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <memory>

namespace OSFG {

// Forward declarations
class FFXLoader;

// Configuration for FFX frame generation
struct FFXFrameGenConfig {
    uint32_t displayWidth = 1920;
    uint32_t displayHeight = 1080;
    uint32_t backBufferCount = 3;
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool enableHDR = false;
    bool enableAsyncCompute = true;
    bool vsync = false;
};

// Statistics from frame generation
struct FFXFrameGenStats {
    uint64_t framesGenerated = 0;
    uint64_t framesPresented = 0;
    float averageFrameTimeMs = 0.0f;
    float lastFrameTimeMs = 0.0f;
    uint64_t gpuMemoryUsageBytes = 0;
};

// FidelityFX Frame Generation wrapper class
class FFXFrameGeneration {
public:
    FFXFrameGeneration();
    ~FFXFrameGeneration();

    // Non-copyable
    FFXFrameGeneration(const FFXFrameGeneration&) = delete;
    FFXFrameGeneration& operator=(const FFXFrameGeneration&) = delete;

    // Initialize with HWND (creates new swap chain)
    bool Initialize(
        ID3D12Device* device,
        ID3D12CommandQueue* commandQueue,
        IDXGIFactory4* dxgiFactory,
        HWND hwnd,
        const FFXFrameGenConfig& config
    );

    // Initialize by wrapping existing swap chain
    bool InitializeWithSwapChain(
        ID3D12Device* device,
        ID3D12CommandQueue* commandQueue,
        IDXGISwapChain4* existingSwapChain
    );

    // Shutdown and release resources
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Configure frame generation parameters
    bool Configure(const FFXFrameGenConfig& config);

    // Enable or disable frame generation
    bool SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

    // Get the FFX-wrapped swap chain
    IDXGISwapChain4* GetSwapChain() const { return m_swapChain.Get(); }

    // Present (replaces normal swap chain Present)
    // Call this instead of swapChain->Present()
    bool Present(uint32_t syncInterval = 0, uint32_t flags = 0);

    // Wait for all pending presents to complete
    void WaitForPendingPresents();

    // Get statistics
    const FFXFrameGenStats& GetStats() const { return m_stats; }

    // Get last error message
    const std::string& GetLastError() const { return m_lastError; }

private:
    bool CreateSwapChainContext(HWND hwnd, const FFXFrameGenConfig& config);
    bool WrapExistingSwapChain(IDXGISwapChain4* swapChain);
    void UpdateStats();

    // D3D12 objects
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGIFactory4> m_dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapChain;

    // FFX context (opaque handle)
    void* m_ffxContext = nullptr;

    // State
    bool m_initialized = false;
    bool m_enabled = true;
    bool m_ownsSwapChain = false;
    FFXFrameGenConfig m_config;
    FFXFrameGenStats m_stats;
    std::string m_lastError;

    // Timing
    LARGE_INTEGER m_lastPresentTime = {};
    LARGE_INTEGER m_frequency = {};
};

} // namespace OSFG
