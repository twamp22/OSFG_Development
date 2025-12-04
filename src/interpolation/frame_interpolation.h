// OSFG Frame Interpolation
// Generates intermediate frames using motion vectors from optical flow
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
#include <d3dcompiler.h>
#include <cstdint>
#include <string>

namespace OSFG {

// Configuration for frame interpolation
struct FrameInterpolationConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    float interpolationFactor = 0.5f;  // 0.0 = previous frame, 1.0 = current frame
};

// Statistics
struct FrameInterpolationStats {
    double lastInterpolationTimeMs = 0.0;  // CPU-side timing
    double avgInterpolationTimeMs = 0.0;
    double lastGpuTimeMs = 0.0;            // GPU timestamp timing
    double avgGpuTimeMs = 0.0;
    uint64_t framesInterpolated = 0;
};

class FrameInterpolation {
public:
    FrameInterpolation();
    ~FrameInterpolation();

    // Non-copyable
    FrameInterpolation(const FrameInterpolation&) = delete;
    FrameInterpolation& operator=(const FrameInterpolation&) = delete;

    // Initialize with D3D12 device
    bool Initialize(ID3D12Device* device, const FrameInterpolationConfig& config);

    // Shutdown
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Set interpolation factor (0.0 to 1.0)
    void SetInterpolationFactor(float factor);

    // Dispatch frame interpolation
    // previousFrame: Previous frame texture
    // currentFrame: Current frame texture
    // motionVectors: Motion vector texture from optical flow (R16G16_SINT)
    // commandList: Command list to record work
    bool Dispatch(ID3D12Resource* previousFrame,
                  ID3D12Resource* currentFrame,
                  ID3D12Resource* motionVectors,
                  ID3D12GraphicsCommandList* commandList);

    // Get interpolated frame texture
    ID3D12Resource* GetInterpolatedFrame() const { return m_interpolatedFrame.Get(); }

    // Get dimensions
    uint32_t GetWidth() const { return m_config.width; }
    uint32_t GetHeight() const { return m_config.height; }

    // Get statistics
    const FrameInterpolationStats& GetStats() const { return m_stats; }

    // Set GPU timestamp frequency (call with command queue before first dispatch)
    void SetTimestampFrequency(ID3D12CommandQueue* cmdQueue);

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

private:
    bool CreateRootSignature();
    bool CreatePipelineState();
    bool CreateResources();
    bool CreateDescriptorHeaps();

    // D3D12 objects
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    // Descriptor heaps
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
    uint32_t m_srvUavDescriptorSize = 0;

    // Resources
    Microsoft::WRL::ComPtr<ID3D12Resource> m_interpolatedFrame;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;

    // Configuration
    FrameInterpolationConfig m_config;

    // State
    bool m_initialized = false;
    FrameInterpolationStats m_stats;
    std::string m_lastError;

    // Cached texture pointers for descriptor caching
    ID3D12Resource* m_cachedPrevFrame = nullptr;
    ID3D12Resource* m_cachedCurrFrame = nullptr;
    ID3D12Resource* m_cachedMotionVectors = nullptr;
    bool m_descriptorsValid = false;

    // GPU timing resources
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_timestampQueryHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_timestampReadbackBuffer;
    uint64_t m_gpuTimestampFrequency = 0;
    bool m_gpuTimingEnabled = false;

    // Constant buffer data (must match shader)
    struct ConstantBufferData {
        uint32_t width;
        uint32_t height;
        uint32_t mvWidth;
        uint32_t mvHeight;
        float interpolationFactor;
        float motionScale;  // Scale factor for motion vectors (1/16 for our format)
        float padding[2];   // Align to 16 bytes
    };
};

} // namespace OSFG
