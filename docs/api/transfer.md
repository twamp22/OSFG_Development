# Transfer Module API

The transfer module (`osfg_transfer`) handles inter-GPU frame transfer for dual-GPU frame generation configurations.

## Header

```cpp
#include "transfer/gpu_transfer.h"
```

## Namespace

```cpp
namespace osfg { ... }
```

## Classes

### GPUTransfer

Main class for transferring frames between GPUs.

#### Static Methods

```cpp
// Enumerate available GPUs
static std::vector<GPUInfo> EnumerateGPUs();

// Check if peer-to-peer transfer is available
static bool IsPeerToPeerAvailable(uint32_t sourceAdapter, uint32_t destAdapter);
```

#### Constructor/Destructor

```cpp
GPUTransfer();
~GPUTransfer();
```

#### Initialization

```cpp
// Initialize transfer engine
bool Initialize(const TransferConfig& config);

// Shutdown and release resources
void Shutdown();

// Check initialization state
bool IsInitialized() const;
```

#### Frame Transfer

```cpp
// Transfer a frame from source GPU to destination GPU
bool TransferFrame(ID3D12Resource* sourceTexture);

// Get the transferred texture on destination GPU
ID3D12Resource* GetDestinationTexture() const;

// Get previous frame texture (for optical flow)
ID3D12Resource* GetPreviousTexture() const;

// Advance to next buffer
void AdvanceBuffer();

// Wait for transfer to complete
void WaitForTransfer();
```

#### Device Access

```cpp
// Get source GPU D3D12 device
ID3D12Device* GetSourceDevice() const;

// Get destination GPU D3D12 device
ID3D12Device* GetDestDevice() const;

// Get destination command queue
ID3D12CommandQueue* GetDestCommandQueue() const;
```

#### Statistics

```cpp
// Get transfer statistics
const TransferStats& GetStats() const;

// Reset statistics
void ResetStats();

// Get current transfer method
TransferMethod GetTransferMethod() const;

// Get last error
const std::string& GetLastError() const;
```

## Structures

### GPUInfo

Information about an available GPU.

```cpp
struct GPUInfo {
    uint32_t adapterIndex;              // Adapter index
    std::wstring description;           // GPU name
    uint64_t dedicatedVideoMemory;      // VRAM size (bytes)
    uint64_t sharedSystemMemory;        // Shared memory (bytes)
    LUID luid;                          // Locally unique identifier
    bool isIntegrated;                  // True if integrated GPU
    bool supportsCrossAdapterRowMajor;  // Cross-adapter texture support
};
```

### TransferConfig

Configuration for transfer initialization.

```cpp
struct TransferConfig {
    uint32_t sourceAdapterIndex = 0;   // Primary GPU index
    uint32_t destAdapterIndex = 1;     // Secondary GPU index
    uint32_t width = 1920;             // Frame width
    uint32_t height = 1080;            // Frame height
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    uint32_t bufferCount = 3;          // Triple buffering
    bool preferPeerToPeer = true;      // Try P2P first
    bool allowCPUFallback = true;      // Allow CPU staging fallback
};
```

### TransferStats

Transfer performance statistics.

```cpp
struct TransferStats {
    uint64_t framesTransferred = 0;    // Total frames transferred
    uint64_t bytesTranferred = 0;      // Total bytes transferred
    double avgTransferTimeMs = 0.0;    // Average transfer time
    double lastTransferTimeMs = 0.0;   // Last transfer time
    double minTransferTimeMs = 0.0;    // Minimum transfer time
    double maxTransferTimeMs = 0.0;    // Maximum transfer time
    double throughputMBps = 0.0;       // Current throughput
    TransferMethod currentMethod;       // Active transfer method
};
```

## Enumerations

### TransferMethod

```cpp
enum class TransferMethod {
    Unknown,           // Not initialized
    PeerToPeer,        // Direct GPU-to-GPU (fastest)
    CrossAdapterHeap,  // D3D12 cross-adapter heap
    StagedCPU          // CPU staging buffer (fallback)
};
```

## Usage Example

```cpp
#include "transfer/gpu_transfer.h"

using namespace osfg;

int main() {
    // Enumerate available GPUs
    auto gpus = GPUTransfer::EnumerateGPUs();
    for (const auto& gpu : gpus) {
        wprintf(L"GPU %d: %s (%.1f GB VRAM)\n",
                gpu.adapterIndex,
                gpu.description.c_str(),
                gpu.dedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0));
    }

    // Check P2P availability
    if (GPUTransfer::IsPeerToPeerAvailable(0, 1)) {
        printf("Peer-to-peer transfer available!\n");
    }

    // Initialize transfer
    GPUTransfer transfer;
    TransferConfig config;
    config.sourceAdapterIndex = 0;
    config.destAdapterIndex = 1;
    config.width = 1920;
    config.height = 1080;

    if (!transfer.Initialize(config)) {
        printf("Error: %s\n", transfer.GetLastError().c_str());
        return 1;
    }

    printf("Using transfer method: %s\n",
           transfer.GetTransferMethod() == TransferMethod::CrossAdapterHeap
           ? "Cross-Adapter Heap" : "CPU Staging");

    // Transfer loop
    while (running) {
        // Get frame from source GPU
        ID3D12Resource* sourceFrame = CaptureFrame();

        // Transfer to destination GPU
        if (transfer.TransferFrame(sourceFrame)) {
            // Get transferred texture for processing
            ID3D12Resource* destFrame = transfer.GetDestinationTexture();
            ID3D12Resource* prevFrame = transfer.GetPreviousTexture();

            // Process on destination GPU
            ProcessFrames(destFrame, prevFrame);

            // Advance buffer for next frame
            transfer.AdvanceBuffer();
        }
    }

    // Print statistics
    const auto& stats = transfer.GetStats();
    printf("Transferred %llu frames\n", stats.framesTransferred);
    printf("Avg transfer time: %.2f ms\n", stats.avgTransferTimeMs);
    printf("Throughput: %.1f MB/s\n", stats.throughputMBps);

    transfer.Shutdown();
    return 0;
}
```

## Transfer Methods

### Cross-Adapter Heap (Preferred)

Uses D3D12 cross-adapter shared heaps for direct GPU-to-GPU transfer.

**Requirements:**
- Windows 10 version 1903+
- Both GPUs support `CrossAdapterRowMajorTextureSupported`
- Same PCIe root complex

**Performance:** ~1-2ms for 1080p

### CPU Staging (Fallback)

Falls back to CPU memory when cross-adapter isn't available.

**Flow:**
1. Copy texture to readback buffer (GPU 0 → CPU)
2. memcpy between buffers (CPU)
3. Copy from upload buffer to texture (CPU → GPU 1)

**Performance:** ~3-5ms for 1080p

## Thread Safety

- `GPUTransfer` is **not thread-safe**
- All methods should be called from the same thread
- GPU work is submitted asynchronously; use `WaitForTransfer()` for synchronization

## Performance Tips

1. **Use triple buffering** (`bufferCount = 3`) to hide transfer latency
2. **Call AdvanceBuffer()** immediately after processing to start next transfer
3. **Prefer cross-adapter** when available; it's 2-3x faster than CPU staging
4. **Monitor throughput** to detect PCIe bottlenecks at higher resolutions
