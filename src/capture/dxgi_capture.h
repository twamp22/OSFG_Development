#pragma once

// OSFG - Open Source Frame Generation
// DXGI Desktop Duplication Capture Engine
//
// This module captures frames from the Windows desktop compositor
// using the DXGI Desktop Duplication API for low-latency frame acquisition.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <functional>
#include <chrono>

namespace osfg {

using Microsoft::WRL::ComPtr;

// Frame capture statistics
struct CaptureStats {
    uint64_t framesCapture = 0;
    uint64_t framesMissed = 0;
    double avgCaptureTimeMs = 0.0;
    double lastCaptureTimeMs = 0.0;
    double minCaptureTimeMs = 1000000.0;
    double maxCaptureTimeMs = 0.0;
};

// Captured frame data
struct CapturedFrame {
    ComPtr<ID3D11Texture2D> texture;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint64_t frameNumber = 0;
    std::chrono::high_resolution_clock::time_point captureTime;
    bool isValid = false;
};

// Configuration for the capture engine
struct CaptureConfig {
    uint32_t outputIndex = 0;          // Which monitor to capture
    uint32_t adapterIndex = 0;         // Which GPU adapter to use
    bool createStagingTexture = false; // Create CPU-readable staging texture
    uint32_t timeoutMs = 16;           // Timeout for frame acquisition (0 = no wait)
};

// DXGI Desktop Duplication capture engine
class DXGICapture {
public:
    DXGICapture();
    ~DXGICapture();

    // Disable copy
    DXGICapture(const DXGICapture&) = delete;
    DXGICapture& operator=(const DXGICapture&) = delete;

    // Initialize the capture engine
    // Returns true on success, false on failure
    bool Initialize(const CaptureConfig& config = CaptureConfig{});

    // Initialize with an external D3D11 device (for interop scenarios)
    bool Initialize(ID3D11Device* externalDevice, const CaptureConfig& config = CaptureConfig{});

    // Shutdown and release all resources
    void Shutdown();

    // Capture the next frame
    // Returns true if a new frame was captured, false if no new frame or error
    bool CaptureFrame(CapturedFrame& outFrame);

    // Release the current frame (must be called before next capture)
    void ReleaseFrame();

    // Get capture statistics
    const CaptureStats& GetStats() const { return m_stats; }

    // Reset statistics
    void ResetStats();

    // Get the D3D11 device (for resource sharing)
    ID3D11Device* GetDevice() const { return m_device.Get(); }

    // Get the D3D11 device context
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }

    // Get the last error message
    const std::string& GetLastError() const { return m_lastError; }

    // Get display dimensions
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

private:
    bool CreateD3D11Device(uint32_t adapterIndex);
    bool InitializeDesktopDuplication(uint32_t outputIndex);
    void SetError(const std::string& error);

    // D3D11 resources
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D> m_stagingTexture;

    // State
    bool m_initialized = false;
    bool m_frameAcquired = false;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint64_t m_frameCounter = 0;
    CaptureConfig m_config;
    CaptureStats m_stats;
    std::string m_lastError;
};

} // namespace osfg
