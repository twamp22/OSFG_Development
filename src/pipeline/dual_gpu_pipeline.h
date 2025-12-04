// OSFG - Open Source Frame Generation
// Dual-GPU Pipeline
//
// Orchestrates the complete frame generation pipeline across two GPUs:
// GPU 0 (Primary): Frame capture
// GPU 1 (Secondary): Optical flow, interpolation, presentation
//
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
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <functional>

// Forward declarations
namespace osfg {
    class DXGICapture;
    struct CapturedFrame;
}

namespace OSFG {
    class D3D11D3D12Interop;
    class SimpleOpticalFlow;
    class FrameInterpolation;
    class SimplePresenter;
}

namespace osfg {

using Microsoft::WRL::ComPtr;

// Frame generation multiplier
enum class FrameMultiplier {
    X2 = 2,     // 60 -> 120 fps
    X3 = 3,     // 60 -> 180 fps
    X4 = 4      // 60 -> 240 fps
};

// Pipeline statistics
struct PipelineStats {
    // Frame counts
    uint64_t baseFamesCaptured = 0;
    uint64_t framesGenerated = 0;
    uint64_t framesPresented = 0;
    uint64_t framesDropped = 0;

    // Timing (milliseconds)
    double captureTimeMs = 0.0;
    double transferTimeMs = 0.0;
    double opticalFlowTimeMs = 0.0;
    double interpolationTimeMs = 0.0;
    double presentTimeMs = 0.0;
    double totalPipelineTimeMs = 0.0;

    // Frame rates
    double baseFPS = 0.0;
    double outputFPS = 0.0;

    // Transfer stats
    double transferThroughputMBps = 0.0;
    bool usingPeerToPeer = false;
};

// Pipeline configuration
struct DualGPUConfig {
    // GPU selection
    uint32_t primaryGPU = 0;        // Capture GPU (usually the gaming GPU)
    uint32_t secondaryGPU = 1;      // Frame generation GPU

    // Resolution
    uint32_t width = 1920;
    uint32_t height = 1080;

    // Frame generation
    FrameMultiplier multiplier = FrameMultiplier::X2;
    bool enableFrameGen = true;

    // Capture settings
    uint32_t captureMonitor = 0;
    uint32_t captureTimeoutMs = 0;  // 0 = non-blocking

    // Presentation
    bool vsync = true;
    bool borderlessWindow = true;
    const wchar_t* windowTitle = L"OSFG Dual-GPU Frame Generation";

    // Transfer settings
    bool preferPeerToPeer = true;
    uint32_t transferBufferCount = 3;

    // Optical flow
    uint32_t opticalFlowBlockSize = 8;
    uint32_t opticalFlowSearchRadius = 12;

    // Advanced
    bool enableOverlay = true;
    bool enableDebugOutput = false;
};

// Callback types
using FrameCallback = std::function<void(uint64_t frameNumber, double frameTimeMs)>;
using ErrorCallback = std::function<void(const std::string& error)>;

// Dual-GPU Frame Generation Pipeline
class DualGPUPipeline {
public:
    DualGPUPipeline();
    ~DualGPUPipeline();

    // Disable copy
    DualGPUPipeline(const DualGPUPipeline&) = delete;
    DualGPUPipeline& operator=(const DualGPUPipeline&) = delete;

    // Initialize the pipeline
    bool Initialize(const DualGPUConfig& config);

    // Shutdown and release all resources
    void Shutdown();

    // Start the pipeline (begins capture and generation)
    bool Start();

    // Stop the pipeline
    void Stop();

    // Check if pipeline is running
    bool IsRunning() const { return m_running; }

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Process one frame (call this in a loop, or let the pipeline run autonomously)
    bool ProcessFrame();

    // Run the pipeline autonomously (blocks until Stop() is called)
    void Run();

    // Enable/disable frame generation at runtime
    void SetFrameGenEnabled(bool enabled);
    bool IsFrameGenEnabled() const { return m_frameGenEnabled; }

    // Change frame multiplier at runtime
    void SetFrameMultiplier(FrameMultiplier multiplier);
    FrameMultiplier GetFrameMultiplier() const { return m_config.multiplier; }

    // Get statistics
    const PipelineStats& GetStats() const { return m_stats; }
    void ResetStats();

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

    // Set callbacks
    void SetFrameCallback(FrameCallback callback) { m_frameCallback = callback; }
    void SetErrorCallback(ErrorCallback callback) { m_errorCallback = callback; }

    // Get window handle (for input handling)
    HWND GetWindowHandle() const;

    // Check if window is still open
    bool IsWindowOpen() const;

private:
    // Initialization helpers
    bool InitializeCapture();
    bool InitializeTransfer();
    bool InitializeCompute();
    bool InitializePresentation();

    // Pipeline stages
    bool CaptureFrame();
    bool TransferFrame();
    bool ComputeOpticalFlow();
    bool GenerateFrames();
    bool PresentFrames();

    // Frame pacing
    void WaitForFramePacing(int frameIndex, int totalFrames);

    // Error handling
    void SetError(const std::string& error);
    void ReportError(const std::string& error);

    // Update statistics
    void UpdateStats();

    // Configuration
    DualGPUConfig m_config;

    // Pipeline components
    std::unique_ptr<DXGICapture> m_capture;
    std::unique_ptr<class GPUTransfer> m_transfer;

    // Secondary GPU resources (for compute)
    ComPtr<ID3D12Device> m_computeDevice;
    ComPtr<ID3D12CommandQueue> m_computeQueue;
    ComPtr<ID3D12CommandAllocator> m_computeAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_computeCommandList;

    // Compute components (on secondary GPU)
    std::unique_ptr<OSFG::SimpleOpticalFlow> m_opticalFlow;
    std::unique_ptr<OSFG::FrameInterpolation> m_interpolation;
    std::unique_ptr<OSFG::SimplePresenter> m_presenter;

    // Synchronization
    ComPtr<ID3D12Fence> m_computeFence;
    HANDLE m_computeFenceEvent = nullptr;
    uint64_t m_computeFenceValue = 0;

    // Frame buffers on secondary GPU
    static const uint32_t MAX_GENERATED_FRAMES = 4;
    ComPtr<ID3D12Resource> m_generatedFrames[MAX_GENERATED_FRAMES];
    uint32_t m_generatedFrameCount = 0;

    // State
    bool m_initialized = false;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_frameGenEnabled{true};
    std::string m_lastError;

    // Statistics
    PipelineStats m_stats;
    std::mutex m_statsMutex;

    // Timing
    std::chrono::high_resolution_clock::time_point m_frameStartTime;
    std::chrono::high_resolution_clock::time_point m_lastPresentTime;
    double m_targetFrameTimeMs = 8.333;  // 120 fps default

    // Callbacks
    FrameCallback m_frameCallback;
    ErrorCallback m_errorCallback;
};

} // namespace osfg
