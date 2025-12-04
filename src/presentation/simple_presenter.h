// OSFG Simple Presenter
// Displays frames in a window using D3D12 swap chain
// MIT License - Part of Open Source Frame Generation project

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
#include <cstdint>
#include <string>

namespace OSFG {

// Configuration for presenter
struct PresenterConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t bufferCount = 2;
    bool vsync = true;
    bool windowed = true;
    const wchar_t* windowTitle = L"OSFG Frame Generation";
};

// Statistics
struct PresenterStats {
    uint64_t framesPresented = 0;
    double lastPresentTimeMs = 0.0;
    double avgPresentTimeMs = 0.0;
    double fps = 0.0;
};

class SimplePresenter {
public:
    SimplePresenter();
    ~SimplePresenter();

    // Non-copyable
    SimplePresenter(const SimplePresenter&) = delete;
    SimplePresenter& operator=(const SimplePresenter&) = delete;

    // Initialize with D3D12 device and command queue
    bool Initialize(ID3D12Device* device,
                   ID3D12CommandQueue* commandQueue,
                   const PresenterConfig& config);

    // Shutdown
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Check if window is still open
    bool IsWindowOpen() const;

    // Process window messages (call this in main loop)
    bool ProcessMessages();

    // Present a frame
    // sourceTexture: The texture to display (must be in PIXEL_SHADER_RESOURCE or COPY_SOURCE state)
    // commandList: Command list to record copy commands (will be closed and executed)
    bool Present(ID3D12Resource* sourceTexture,
                 ID3D12GraphicsCommandList* commandList);

    // Execute swap chain present and advance to next frame
    // Call this after executing the command list from Present()
    bool Flip(uint32_t syncInterval = 1, uint32_t flags = 0);

    // Get current back buffer for rendering
    ID3D12Resource* GetCurrentBackBuffer();
    uint32_t GetCurrentBackBufferIndex() const { return m_frameIndex; }

    // Get window handle
    HWND GetHWND() const { return m_hwnd; }

    // Get dimensions
    uint32_t GetWidth() const { return m_config.width; }
    uint32_t GetHeight() const { return m_config.height; }

    // Get statistics
    const PresenterStats& GetStats() const { return m_stats; }

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

private:
    bool CreatePresenterWindow();
    bool CreateSwapChain();
    bool CreateRenderTargets();
    bool CreateSyncObjects();
    void WaitForGPU();

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // D3D12 objects
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    // Back buffers
    static const uint32_t MAX_BACK_BUFFERS = 3;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_backBuffers[MAX_BACK_BUFFERS];
    uint32_t m_rtvDescriptorSize = 0;

    // Synchronization
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[MAX_BACK_BUFFERS] = {};
    HANDLE m_fenceEvent = nullptr;

    // Window
    HWND m_hwnd = nullptr;
    HINSTANCE m_hinstance = nullptr;
    bool m_windowClosed = false;

    // Configuration and state
    PresenterConfig m_config;
    uint32_t m_frameIndex = 0;
    bool m_initialized = false;
    PresenterStats m_stats;
    std::string m_lastError;

    // Timing
    LARGE_INTEGER m_lastPresentTime = {};
    LARGE_INTEGER m_frequency = {};
};

} // namespace OSFG
