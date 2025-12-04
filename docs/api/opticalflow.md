# Optical Flow Module API

The optical flow module (`osfg_simple_opticalflow`) provides motion estimation using block-matching algorithms on the GPU.

## Header

```cpp
#include "opticalflow/simple_opticalflow.h"
```

## Namespace

```cpp
namespace OSFG { ... }
```

## Classes

### SimpleOpticalFlow

GPU-accelerated block-matching optical flow.

#### Constructor/Destructor

```cpp
SimpleOpticalFlow();
~SimpleOpticalFlow();
```

#### Initialization

```cpp
// Initialize with D3D12 device
bool Initialize(ID3D12Device* device,
                ID3D12CommandQueue* commandQueue,
                const OpticalFlowConfig& config);

// Shutdown and release resources
void Shutdown();

// Check initialization state
bool IsInitialized() const;
```

#### Motion Estimation

```cpp
// Compute optical flow between two frames
// prevFrame: Previous frame texture
// currFrame: Current frame texture
// Returns true on success
bool ComputeFlow(ID3D12Resource* prevFrame,
                 ID3D12Resource* currFrame,
                 ID3D12GraphicsCommandList* commandList);

// Get motion vector output
ID3D12Resource* GetMotionVectors() const;

// Get motion vector dimensions
uint32_t GetMotionVectorWidth() const;
uint32_t GetMotionVectorHeight() const;
```

#### Statistics

```cpp
// Get computation statistics
const OpticalFlowStats& GetStats() const;

// Reset statistics
void ResetStats();

// Get last error
const std::string& GetLastError() const;
```

## Structures

### OpticalFlowConfig

```cpp
struct OpticalFlowConfig {
    uint32_t width = 1920;          // Input frame width
    uint32_t height = 1080;         // Input frame height
    uint32_t blockSize = 8;         // Block size (pixels)
    uint32_t searchRadius = 12;     // Search radius (pixels)
    DXGI_FORMAT inputFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
};
```

### OpticalFlowStats

```cpp
struct OpticalFlowStats {
    uint64_t framesProcessed = 0;
    double avgComputeTimeMs = 0.0;
    double lastComputeTimeMs = 0.0;
};
```

## Algorithm Overview

The block-matching optical flow algorithm:

1. **Luminance Conversion**: Convert RGB frames to grayscale
2. **Block Division**: Divide frame into 8x8 pixel blocks
3. **Search**: For each block in frame N, search for best match in frame N-1 within search radius
4. **SAD Matching**: Use Sum of Absolute Differences for block comparison
5. **Output**: Motion vector (dx, dy) per block stored as int16x2

## Motion Vector Format

Output is stored as `DXGI_FORMAT_R16G16_SINT`:
- R channel: Horizontal motion (dx)
- G channel: Vertical motion (dy)
- Dimensions: (width/blockSize) × (height/blockSize)

## Usage Example

```cpp
#include "opticalflow/simple_opticalflow.h"

using namespace OSFG;

SimpleOpticalFlow opticalFlow;

// Configure
OpticalFlowConfig config;
config.width = 1920;
config.height = 1080;
config.blockSize = 8;
config.searchRadius = 12;

// Initialize
if (!opticalFlow.Initialize(device, commandQueue, config)) {
    printf("Error: %s\n", opticalFlow.GetLastError().c_str());
    return;
}

// Compute flow
if (opticalFlow.ComputeFlow(prevFrame, currFrame, commandList)) {
    // Get motion vectors for interpolation
    ID3D12Resource* motionVectors = opticalFlow.GetMotionVectors();

    // Motion vector dimensions
    uint32_t mvWidth = opticalFlow.GetMotionVectorWidth();   // 1920/8 = 240
    uint32_t mvHeight = opticalFlow.GetMotionVectorHeight(); // 1080/8 = 135
}

// Statistics
const auto& stats = opticalFlow.GetStats();
printf("Optical flow time: %.2f ms\n", stats.lastComputeTimeMs);
```

## Shader Pipeline

The optical flow compute shader (`optical_flow.hlsl`) performs:

```hlsl
// Per-block thread group
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    // 1. Load current block
    // 2. Search previous frame in radius
    // 3. Find minimum SAD match
    // 4. Output motion vector
}
```

## Performance Characteristics

| Resolution | Block Size | Motion Vectors | Typical Time |
|------------|------------|----------------|--------------|
| 1080p | 8×8 | 240×135 | ~2-3ms |
| 1440p | 8×8 | 320×180 | ~3-4ms |
| 4K | 8×8 | 480×270 | ~5-7ms |

## Thread Safety

- `SimpleOpticalFlow` is **not thread-safe**
- Submit commands to a single command list
- Use fences to synchronize GPU work

## Limitations

- Block-based (not per-pixel)
- No sub-pixel precision
- Simple SAD matching (no hierarchical search)
- No scene change detection

For higher quality, consider FSR 3 optical flow integration (Phase 2).
