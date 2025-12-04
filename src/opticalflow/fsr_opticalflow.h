// OSFG - Open Source Frame Generation
// FSR 3 Optical Flow Wrapper
//
// Wraps AMD FidelityFX SDK optical flow for high-quality motion estimation.
// Uses the pre-built signed binaries from the FidelityFX SDK.
//
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
#include <string>
#include <vector>
#include <memory>

namespace OSFG {

// Configuration for FSR optical flow
struct FSROpticalFlowConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool enableTexture1D = false;  // Use 1D textures if supported
};

// Statistics
struct FSROpticalFlowStats {
    double lastDispatchTimeMs = 0.0;
    double avgDispatchTimeMs = 0.0;
    uint64_t framesProcessed = 0;
    size_t gpuMemoryUsageBytes = 0;
};

// FSR 3 Optical Flow wrapper class
class FSROpticalFlow {
public:
    FSROpticalFlow();
    ~FSROpticalFlow();

    // Non-copyable
    FSROpticalFlow(const FSROpticalFlow&) = delete;
    FSROpticalFlow& operator=(const FSROpticalFlow&) = delete;

    // Check if FSR optical flow is available and functional
    // Returns false until full integration is implemented
    static bool IsAvailable();

    // Check if FidelityFX DLL is present (may not be usable yet)
    static bool IsDllPresent();

    // Get path to loaded FidelityFX DLL (for diagnostics)
    static const std::wstring& GetDllPath();

    // Initialize with D3D12 device
    bool Initialize(ID3D12Device* device, const FSROpticalFlowConfig& config);

    // Shutdown and release resources
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Dispatch optical flow computation
    // currentFrame: Current frame texture (color buffer)
    // commandList: Command list to record work
    // reset: Set to true if camera moved discontinuously (scene cut)
    bool Dispatch(ID3D12Resource* currentFrame,
                  ID3D12GraphicsCommandList* commandList,
                  bool reset = false);

    // Get motion vector texture (output)
    // Format: R16G16_SINT with motion vectors in pixels
    ID3D12Resource* GetMotionVectorTexture() const;

    // Get scene change detection texture (output)
    // Used for detecting scene cuts
    ID3D12Resource* GetSceneChangeTexture() const;

    // Get optical flow dimensions (may differ from input)
    uint32_t GetOpticalFlowWidth() const { return m_ofWidth; }
    uint32_t GetOpticalFlowHeight() const { return m_ofHeight; }

    // Get statistics
    const FSROpticalFlowStats& GetStats() const { return m_stats; }

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

private:
    bool CreateBackendInterface();
    bool CreateOpticalFlowContext();
    bool CreateSharedResources();

    // D3D12 objects
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;

    // FidelityFX backend (opaque - stored as raw memory)
    std::vector<uint8_t> m_scratchBuffer;
    void* m_backendInterface = nullptr;  // FfxInterface*
    void* m_opticalFlowContext = nullptr;  // FfxOpticalflowContext*

    // Shared resources (created by us, used by FFX)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_opticalFlowVector;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_opticalFlowSCD;

    // Configuration
    FSROpticalFlowConfig m_config;
    uint32_t m_ofWidth = 0;
    uint32_t m_ofHeight = 0;

    // State
    bool m_initialized = false;
    FSROpticalFlowStats m_stats;
    std::string m_lastError;
};

} // namespace OSFG
