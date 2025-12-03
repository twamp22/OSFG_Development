// OSFG Optical Flow Module
// Wraps AMD FidelityFX FSR 3 Optical Flow for standalone use
// MIT License - Part of Open Source Frame Generation project

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>

// Forward declarations for FidelityFX types
struct FfxInterface;
struct FfxOpticalflowContext;

namespace OSFG {

// Configuration for optical flow initialization
struct OpticalFlowConfig {
    uint32_t width = 1920;           // Input resolution width
    uint32_t height = 1080;          // Input resolution height
    bool enableHDR = false;          // HDR input support
    bool enableFP16 = true;          // Use FP16 where possible
};

// Output from optical flow dispatch
struct OpticalFlowOutput {
    ID3D12Resource* motionVectors;   // R16G16_SINT motion vector texture
    ID3D12Resource* sceneChangeData; // Scene change detection output
    uint32_t motionVectorWidth;      // Motion vector texture width (input/8)
    uint32_t motionVectorHeight;     // Motion vector texture height (input/8)
    bool sceneChangeDetected;        // True if significant scene change
};

// Statistics for performance monitoring
struct OpticalFlowStats {
    double lastDispatchTimeMs;       // Time for last dispatch
    double avgDispatchTimeMs;        // Rolling average dispatch time
    uint64_t totalFramesProcessed;   // Total frames processed
    uint64_t sceneChanges;           // Number of detected scene changes
};

class OpticalFlow {
public:
    OpticalFlow();
    ~OpticalFlow();

    // Non-copyable
    OpticalFlow(const OpticalFlow&) = delete;
    OpticalFlow& operator=(const OpticalFlow&) = delete;

    // Initialize optical flow with DX12 device and command queue
    // Returns true on success
    bool Initialize(ID3D12Device* device,
                   ID3D12CommandQueue* commandQueue,
                   const OpticalFlowConfig& config);

    // Shutdown and release all resources
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Process a frame and compute optical flow
    // inputTexture: The current frame (RGBA format recommended)
    // commandList: A command list to record compute work
    // Returns true on success
    bool Dispatch(ID3D12Resource* inputTexture,
                  ID3D12GraphicsCommandList* commandList,
                  bool reset = false);

    // Get the optical flow output after dispatch
    const OpticalFlowOutput& GetOutput() const { return m_output; }

    // Get performance statistics
    const OpticalFlowStats& GetStats() const { return m_stats; }

    // Get required motion vector texture size for given input resolution
    static void GetMotionVectorSize(uint32_t inputWidth, uint32_t inputHeight,
                                   uint32_t& outWidth, uint32_t& outHeight);

    // Get current configuration
    const OpticalFlowConfig& GetConfig() const { return m_config; }

private:
    bool CreateResources();
    void DestroyResources();
    bool CreateFfxContext();
    void DestroyFfxContext();

    // DX12 objects
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;

    // Output resources (owned by this class)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_motionVectorTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneChangeTexture;

    // FidelityFX interface and context
    std::unique_ptr<uint8_t[]> m_scratchBuffer;
    size_t m_scratchBufferSize = 0;
    FfxInterface* m_ffxInterface = nullptr;
    FfxOpticalflowContext* m_ffxContext = nullptr;

    // State
    bool m_initialized = false;
    OpticalFlowConfig m_config;
    OpticalFlowOutput m_output;
    OpticalFlowStats m_stats;

    // Frame tracking
    uint32_t m_frameIndex = 0;
};

} // namespace OSFG
