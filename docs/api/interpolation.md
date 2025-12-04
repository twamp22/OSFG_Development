# Interpolation Module API

The interpolation module (`osfg_interpolation`) generates intermediate frames using motion-compensated warping.

## Header

```cpp
#include "interpolation/frame_interpolation.h"
```

## Namespace

```cpp
namespace OSFG { ... }
```

## Classes

### FrameInterpolation

GPU-accelerated frame interpolation using motion vectors.

#### Constructor/Destructor

```cpp
FrameInterpolation();
~FrameInterpolation();
```

#### Initialization

```cpp
// Initialize with D3D12 device
bool Initialize(ID3D12Device* device,
                ID3D12CommandQueue* commandQueue,
                const InterpolationConfig& config);

// Shutdown and release resources
void Shutdown();

// Check initialization state
bool IsInitialized() const;
```

#### Frame Generation

```cpp
// Generate interpolated frame
// prevFrame: Previous frame (N-1)
// currFrame: Current frame (N)
// motionVectors: Motion vectors from optical flow
// t: Interpolation factor (0.0 = prevFrame, 1.0 = currFrame, typically 0.5)
bool Interpolate(ID3D12Resource* prevFrame,
                 ID3D12Resource* currFrame,
                 ID3D12Resource* motionVectors,
                 float t,
                 ID3D12GraphicsCommandList* commandList);

// Get interpolated frame output
ID3D12Resource* GetOutputFrame() const;
```

#### Statistics

```cpp
// Get interpolation statistics
const InterpolationStats& GetStats() const;

// Reset statistics
void ResetStats();

// Get last error
const std::string& GetLastError() const;
```

## Structures

### InterpolationConfig

```cpp
struct InterpolationConfig {
    uint32_t width = 1920;              // Frame width
    uint32_t height = 1080;             // Frame height
    uint32_t motionBlockSize = 8;       // Motion vector block size
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bool enableHoleFilling = true;      // Fill disoccluded regions
    bool enableBlending = true;         // Blend forward/backward warps
};
```

### InterpolationStats

```cpp
struct InterpolationStats {
    uint64_t framesGenerated = 0;
    double avgInterpolationTimeMs = 0.0;
    double lastInterpolationTimeMs = 0.0;
};
```

## Algorithm Overview

The frame interpolation algorithm:

1. **Motion Scaling**: Scale motion vectors by interpolation factor `t`
2. **Forward Warp**: Warp previous frame toward interpolation point
3. **Backward Warp**: Warp current frame toward interpolation point
4. **Blending**: Blend forward and backward warps
5. **Hole Filling**: Fill disoccluded regions

```
Previous Frame (N-1)                 Current Frame (N)
       │                                    │
       │ Forward warp                       │ Backward warp
       │ (motion × t)                       │ (motion × (1-t))
       ▼                                    ▼
   ┌────────┐                          ┌────────┐
   │Warped 1│◄─────── Blend ──────────►│Warped 2│
   └────────┘           │              └────────┘
                        ▼
               ┌──────────────┐
               │ Output Frame │
               │   (N-0.5)    │
               └──────────────┘
```

## Usage Example

```cpp
#include "interpolation/frame_interpolation.h"

using namespace OSFG;

FrameInterpolation interpolation;

// Configure
InterpolationConfig config;
config.width = 1920;
config.height = 1080;
config.motionBlockSize = 8;
config.enableHoleFilling = true;

// Initialize
if (!interpolation.Initialize(device, commandQueue, config)) {
    printf("Error: %s\n", interpolation.GetLastError().c_str());
    return;
}

// Generate interpolated frame
float t = 0.5f;  // Midpoint between frames
if (interpolation.Interpolate(prevFrame, currFrame, motionVectors, t, commandList)) {
    ID3D12Resource* generatedFrame = interpolation.GetOutputFrame();

    // Use generated frame for presentation
    PresentFrame(generatedFrame);
}

// Statistics
const auto& stats = interpolation.GetStats();
printf("Interpolation time: %.2f ms\n", stats.lastInterpolationTimeMs);
```

## Multiple Frame Generation (3X/4X Mode)

For generating multiple intermediate frames:

```cpp
// 3X mode: Generate 2 intermediate frames per base frame
float t_values[] = { 0.33f, 0.67f };

for (float t : t_values) {
    interpolation.Interpolate(prevFrame, currFrame, motionVectors, t, commandList);
    PresentFrame(interpolation.GetOutputFrame());
}
PresentFrame(currFrame);  // Present original frame

// 4X mode: Generate 3 intermediate frames
float t_values_4x[] = { 0.25f, 0.5f, 0.75f };
```

## Interpolation Quality

### Hole Filling

Disocclusions occur when:
- Objects move, revealing previously hidden areas
- Fast motion causes gaps in warped image

Hole filling strategies:
1. **Neighbor sampling**: Use adjacent pixels
2. **Background layer**: Prefer "further" pixels
3. **Inpainting**: Fill based on surrounding context

### Blending Weights

Blend weights can be uniform (0.5/0.5) or adaptive based on:
- Motion magnitude
- Occlusion confidence
- Edge detection

## Performance Characteristics

| Resolution | Typical Time | Memory Usage |
|------------|--------------|--------------|
| 1080p | ~3-4ms | ~32 MB |
| 1440p | ~4-6ms | ~56 MB |
| 4K | ~7-10ms | ~128 MB |

## Shader Pipeline

The interpolation compute shader performs:

```hlsl
[numthreads(8, 8, 1)]
void CSInterpolate(uint3 DTid : SV_DispatchThreadID) {
    // 1. Read motion vector for this pixel
    // 2. Calculate source positions in prev/curr frames
    // 3. Sample both frames with bilinear filtering
    // 4. Blend samples based on t and occlusion
    // 5. Write output pixel
}
```

## Thread Safety

- `FrameInterpolation` is **not thread-safe**
- Submit all commands to a single command list
- Use fences for GPU synchronization

## Known Artifacts

| Artifact | Cause | Mitigation |
|----------|-------|------------|
| Ghosting | Incorrect motion | Improve optical flow |
| Tearing | Motion boundaries | Edge-aware blending |
| Holes | Disocclusion | Enable hole filling |
| Blur | Over-blending | Reduce blend radius |
