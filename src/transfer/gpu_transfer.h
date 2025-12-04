// OSFG - Open Source Frame Generation
// Inter-GPU Transfer Engine
//
// This module handles frame transfer between GPUs for dual-GPU frame generation.
// Supports both peer-to-peer transfers (when available) and staged CPU transfers.
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
#include <vector>
#include <memory>
#include <chrono>

namespace osfg {

using Microsoft::WRL::ComPtr;

// GPU information
struct GPUInfo {
    uint32_t adapterIndex;
    std::wstring description;
    uint64_t dedicatedVideoMemory;
    uint64_t sharedSystemMemory;
    LUID luid;
    bool isIntegrated;
    bool supportsCrossAdapterRowMajor;
};

// Transfer method
enum class TransferMethod {
    Unknown,
    PeerToPeer,         // Direct GPU-to-GPU via PCIe (fastest, not always available)
    CrossAdapterHeap,   // D3D12 cross-adapter heap (Windows 10+)
    StagedCPU           // CPU staging buffer (fallback, always works)
};

// Transfer statistics
struct TransferStats {
    uint64_t framesTransferred = 0;
    uint64_t bytesTranferred = 0;
    double avgTransferTimeMs = 0.0;
    double lastTransferTimeMs = 0.0;
    double minTransferTimeMs = 1000000.0;
    double maxTransferTimeMs = 0.0;
    double throughputMBps = 0.0;
    TransferMethod currentMethod = TransferMethod::Unknown;
};

// Configuration for GPU transfer
struct TransferConfig {
    uint32_t sourceAdapterIndex = 0;     // Primary GPU (game/capture)
    uint32_t destAdapterIndex = 1;       // Secondary GPU (frame generation)
    uint32_t width = 1920;
    uint32_t height = 1080;
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    uint32_t bufferCount = 3;            // Triple buffer for latency hiding
    bool preferPeerToPeer = true;        // Try P2P first
    bool allowCPUFallback = true;        // Fall back to CPU staging if needed
};

// Inter-GPU transfer engine
class GPUTransfer {
public:
    GPUTransfer();
    ~GPUTransfer();

    // Disable copy
    GPUTransfer(const GPUTransfer&) = delete;
    GPUTransfer& operator=(const GPUTransfer&) = delete;

    // Enumerate available GPUs
    static std::vector<GPUInfo> EnumerateGPUs();

    // Check if peer-to-peer transfer is available between two GPUs
    static bool IsPeerToPeerAvailable(uint32_t sourceAdapter, uint32_t destAdapter);

    // Initialize transfer engine
    bool Initialize(const TransferConfig& config);

    // Shutdown and release resources
    void Shutdown();

    // Transfer a frame from source GPU to destination GPU
    // sourceTexture: Texture on source GPU (must be in COPY_SOURCE state)
    // Returns true on success
    bool TransferFrame(ID3D12Resource* sourceTexture);

    // Get the transferred texture on the destination GPU
    // Returns the most recently transferred frame (in PIXEL_SHADER_RESOURCE state)
    ID3D12Resource* GetDestinationTexture() const;

    // Get the previous frame texture (for optical flow)
    ID3D12Resource* GetPreviousTexture() const;

    // Advance to next buffer (call after processing current frame)
    void AdvanceBuffer();

    // Wait for transfer to complete
    void WaitForTransfer();

    // Get source GPU D3D12 device
    ID3D12Device* GetSourceDevice() const { return m_sourceDevice.Get(); }

    // Get destination GPU D3D12 device
    ID3D12Device* GetDestDevice() const { return m_destDevice.Get(); }

    // Get destination command queue (for frame generation work)
    ID3D12CommandQueue* GetDestCommandQueue() const { return m_destCommandQueue.Get(); }

    // Get transfer statistics
    const TransferStats& GetStats() const { return m_stats; }

    // Reset statistics
    void ResetStats();

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Get current transfer method
    TransferMethod GetTransferMethod() const { return m_transferMethod; }

private:
    bool CreateDevices();
    bool CreateCrossAdapterResources();
    bool CreateStagingResources();
    bool CreateSyncObjects();
    void SetError(const std::string& error);

    // Cross-adapter transfer implementation
    bool TransferViaCrossAdapter(ID3D12Resource* sourceTexture);

    // Staged CPU transfer implementation (fallback)
    bool TransferViaStaging(ID3D12Resource* sourceTexture);

    // Source GPU resources
    ComPtr<ID3D12Device> m_sourceDevice;
    ComPtr<ID3D12CommandQueue> m_sourceCommandQueue;
    ComPtr<ID3D12CommandAllocator> m_sourceCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_sourceCommandList;

    // Destination GPU resources
    ComPtr<ID3D12Device> m_destDevice;
    ComPtr<ID3D12CommandQueue> m_destCommandQueue;
    ComPtr<ID3D12CommandAllocator> m_destCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_destCommandList;

    // Cross-adapter shared resources (heap-based sharing)
    ComPtr<ID3D12Heap> m_crossAdapterHeap;
    std::vector<ComPtr<ID3D12Resource>> m_crossAdapterTextures;  // On source GPU
    std::vector<ComPtr<ID3D12Resource>> m_destTextures;          // On dest GPU

    // CPU staging resources (fallback path)
    ComPtr<ID3D12Resource> m_sourceReadbackBuffer;
    ComPtr<ID3D12Resource> m_destUploadBuffer;
    void* m_stagingMemory = nullptr;
    size_t m_stagingSize = 0;

    // Synchronization
    ComPtr<ID3D12Fence> m_sourceFence;
    ComPtr<ID3D12Fence> m_destFence;
    HANDLE m_sourceFenceEvent = nullptr;
    HANDLE m_destFenceEvent = nullptr;
    uint64_t m_sourceFenceValue = 0;
    uint64_t m_destFenceValue = 0;

    // Shared fence for cross-adapter sync
    ComPtr<ID3D12Fence> m_sharedFence;
    HANDLE m_sharedFenceHandle = nullptr;

    // State
    TransferConfig m_config;
    TransferMethod m_transferMethod = TransferMethod::Unknown;
    bool m_initialized = false;
    uint32_t m_currentBuffer = 0;
    uint32_t m_previousBuffer = 0;
    TransferStats m_stats;
    std::string m_lastError;

    // Timing
    std::chrono::high_resolution_clock::time_point m_transferStart;
};

} // namespace osfg
