// OSFG Simple Optical Flow
// Block-matching optical flow for Phase 1 proof of concept
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
#include <vector>
#include <string>

namespace OSFG {

// Configuration for simple optical flow
struct SimpleOpticalFlowConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t blockSize = 8;      // Block size for matching
    uint32_t searchRadius = 16;  // Search radius in pixels
};

// Statistics
struct SimpleOpticalFlowStats {
    double lastDispatchTimeMs = 0.0;
    double avgDispatchTimeMs = 0.0;
    uint64_t framesProcessed = 0;
};

class SimpleOpticalFlow {
public:
    SimpleOpticalFlow();
    ~SimpleOpticalFlow();

    // Non-copyable
    SimpleOpticalFlow(const SimpleOpticalFlow&) = delete;
    SimpleOpticalFlow& operator=(const SimpleOpticalFlow&) = delete;

    // Initialize with D3D12 device
    bool Initialize(ID3D12Device* device, const SimpleOpticalFlowConfig& config);

    // Shutdown
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Dispatch optical flow computation
    // currentFrame: Current frame texture (SRV)
    // previousFrame: Previous frame texture (SRV)
    // commandList: Command list to record work
    bool Dispatch(ID3D12Resource* currentFrame,
                  ID3D12Resource* previousFrame,
                  ID3D12GraphicsCommandList* commandList);

    // Get motion vector texture
    ID3D12Resource* GetMotionVectorTexture() const { return m_motionVectorTexture.Get(); }

    // Get motion vector dimensions
    uint32_t GetMotionVectorWidth() const { return m_mvWidth; }
    uint32_t GetMotionVectorHeight() const { return m_mvHeight; }

    // Get statistics
    const SimpleOpticalFlowStats& GetStats() const { return m_stats; }

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
    Microsoft::WRL::ComPtr<ID3D12Resource> m_motionVectorTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;

    // Configuration
    SimpleOpticalFlowConfig m_config;
    uint32_t m_mvWidth = 0;
    uint32_t m_mvHeight = 0;

    // State
    bool m_initialized = false;
    SimpleOpticalFlowStats m_stats;
    std::string m_lastError;

    // Constant buffer data
    struct ConstantBufferData {
        uint32_t inputWidth;
        uint32_t inputHeight;
        uint32_t outputWidth;
        uint32_t outputHeight;
        uint32_t blockSize;
        uint32_t searchRadius;
        float minLuminance;
        float maxLuminance;
    };
};

} // namespace OSFG
