# FidelityFX Frame Generation Integration Design

This document outlines the design for integrating AMD FidelityFX Frame Generation into OSFG.

## Current Status

- FidelityFX SDK v2.0 built successfully
- Pre-compiled DLLs available:
  - `amd_fidelityfx_framegeneration_dx12.dll`
  - `amd_fidelityfx_loader_dx12.dll`
  - `amd_fidelityfx_upscaler_dx12.dll`
- DLLs are automatically copied to build output

## FidelityFX API Architecture

The FidelityFX frame generation API is designed for **in-game integration**, not desktop capture. Key components:

### Swap Chain Context (DX12)

```cpp
// Wrap existing swap chain
ffxCreateContextDescFrameGenerationSwapChainWrapDX12 wrapDesc;
wrapDesc.swapchain = &pSwapChain;  // Wraps existing
wrapDesc.gameQueue = pCommandQueue;

// Or create new swap chain for HWND
ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 hwndDesc;
hwndDesc.hwnd = hWnd;
hwndDesc.swapchain = &pSwapChain;
hwndDesc.dxgiFactory = pFactory;
hwndDesc.gameQueue = pCommandQueue;
```

### Frame Generation Context

```cpp
ffxCreateContextDescFrameGeneration fgDesc;
fgDesc.displaySize = { width, height };
fgDesc.maxRenderSize = { width, height };
fgDesc.flags = FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
fgDesc.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
```

### Dispatch Flow

1. **Prepare Phase** - Provide depth, motion vectors, camera info
2. **Generation Phase** - Generate interpolated frames
3. **Present Phase** - FFX handles presentation with frame pacing

## Integration Options for OSFG

### Option 1: Full FFX Pipeline (Recommended)

Replace OSFG's presentation layer with FFX swap chain management.

```
[DXGI Capture] → [D3D11/D3D12 Interop] → [FFX Frame Generation Context]
                                                    ↓
                                         [FFX Swap Chain Context]
                                                    ↓
                                              [Display]
```

**Pros:**
- Best quality frame generation
- Built-in frame pacing
- Scene change detection

**Cons:**
- Requires restructuring presentation
- Less control over output timing

### Option 2: FFX Optical Flow Only

Use FFX optical flow internally but keep OSFG presentation.

**Status:** Not directly supported by signed DLLs.
The optical flow is bundled inside frame generation and not exposed separately.

### Option 3: Hybrid Approach

Use FFX for applications that support it, fall back to SimpleOpticalFlow.

```cpp
class FrameGenerator {
    bool m_useFFX;
    std::unique_ptr<FFXFrameGen> m_ffxGen;
    std::unique_ptr<SimpleOpticalFlow> m_simpleOF;

    void Generate(ID3D12Resource* input) {
        if (m_useFFX && m_ffxGen->IsAvailable()) {
            m_ffxGen->Generate(input);
        } else {
            m_simpleOF->Process(input);
            m_interpolation->Generate(m_simpleOF->GetMotionVectors());
        }
    }
};
```

## Implementation Plan

### Phase 1: FFX Loader Module

Create `osfg_ffx_loader` library:

```cpp
class FFXLoader {
public:
    static bool IsAvailable();
    static bool LoadLibrary();

    // Function pointers to DLL exports
    PfnFfxCreateContext ffxCreateContext;
    PfnFfxDestroyContext ffxDestroyContext;
    PfnFfxConfigure ffxConfigure;
    PfnFfxQuery ffxQuery;
    PfnFfxDispatch ffxDispatch;
};
```

### Phase 2: FFX Frame Generation Wrapper

Create `osfg_ffx_framegen` library:

```cpp
class FFXFrameGeneration {
public:
    bool Initialize(ID3D12Device* device, HWND hwnd);
    void Configure(const FFXFrameGenConfig& config);

    // Prepare phase - call before Present
    void Prepare(const FFXPrepareParams& params);

    // Present - replaces normal swap chain present
    void Present();

    // Statistics
    const FFXStats& GetStats() const;
};
```

### Phase 3: Integration with Pipeline

Modify `DualGPUPipeline` to support FFX backend:

```cpp
class DualGPUPipeline {
    enum class Backend {
        Native,      // SimpleOpticalFlow + OSFG Interpolation
        FidelityFX   // Full FFX frame generation
    };

    void SetBackend(Backend backend);
};
```

## API Usage Example

```cpp
#include "ffx/ffx_loader.h"
#include "ffx/ffx_framegen.h"

// Check availability
if (!OSFG::FFXLoader::IsAvailable()) {
    // Fall back to SimpleOpticalFlow
    return;
}

// Initialize
OSFG::FFXFrameGeneration ffxGen;
if (!ffxGen.Initialize(device, hwnd)) {
    printf("FFX init failed: %s\n", ffxGen.GetLastError().c_str());
    return;
}

// Configure
OSFG::FFXFrameGenConfig config;
config.displayWidth = 1920;
config.displayHeight = 1080;
config.enableAsyncCompute = true;
ffxGen.Configure(config);

// In render loop
OSFG::FFXPrepareParams params;
params.depth = depthTexture;
params.motionVectors = motionVectorTexture;
params.frameTimeDelta = deltaTime;
params.jitterOffset = { jitterX, jitterY };

ffxGen.Prepare(params);
ffxGen.Present();  // Replaces swapChain->Present()
```

## Required Changes

### CMakeLists.txt

```cmake
# FFX Loader Library
add_library(osfg_ffx_loader STATIC
    src/ffx/ffx_loader.cpp
    src/ffx/ffx_loader.h
)

target_link_libraries(osfg_ffx_loader PUBLIC
    ${FFX_LIB_DIR}/amd_fidelityfx_loader_dx12.lib
)

# FFX Frame Generation Wrapper
add_library(osfg_ffx_framegen STATIC
    src/ffx/ffx_framegen.cpp
    src/ffx/ffx_framegen.h
)

target_include_directories(osfg_ffx_framegen PUBLIC
    ${FFX_INCLUDE_DIRS}
)
```

### File Structure

```
src/
├── ffx/
│   ├── ffx_loader.cpp
│   ├── ffx_loader.h
│   ├── ffx_framegen.cpp
│   ├── ffx_framegen.h
│   └── ffx_types.h
```

## Considerations

### Swap Chain Ownership

FFX frame generation requires owning or wrapping the swap chain. For OSFG:
- Demo app can create FFX swap chain directly
- For capture scenarios, may need different approach

### Motion Vector Source

FFX can use:
1. **Internal motion estimation** - From frame differences (like OSFG's SimpleOpticalFlow)
2. **Engine motion vectors** - From game's render pass (not applicable to capture)

For OSFG capture mode, FFX would use internal motion estimation.

### Latency

FFX frame generation adds latency for buffering. Consider:
- Frame pacing reduces perceived stutter
- May increase input lag slightly
- Configurable trade-offs available

## Completed

1. [x] Create FFX loader module (`osfg_ffx_loader`)
2. [x] Implement FFX frame generation wrapper (`osfg_ffx_framegen`)
3. [x] Create test applications (`test_ffx_loader`, `test_ffx_framegen`)
4. [x] Add backend selection to pipeline (`FrameGenBackend` enum)
5. [x] Update test application with backend display

## Backend Selection

The `DualGPUPipeline` now supports runtime backend selection:

```cpp
enum class FrameGenBackend {
    Native,     // SimpleOpticalFlow + OSFG Interpolation (default, no dependencies)
    FidelityFX, // AMD FidelityFX Frame Generation (higher quality, requires FFX DLLs)
    Auto        // Automatically select best available backend
};

// In config
DualGPUConfig config;
config.backend = FrameGenBackend::Auto;  // Auto-select best available

// Check availability
bool ffxAvailable = DualGPUPipeline::IsFidelityFXAvailable();

// After initialization, get active backend
FrameGenBackend active = pipeline.GetActiveBackend();
```

## Next Steps

1. [ ] Benchmark against SimpleOpticalFlow
2. [ ] Add UI resource composition support
3. [ ] Implement frame pacing tuning
4. [ ] Add GPU requirements detection for FFX backend

## References

- [FidelityFX SDK Documentation](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)
- [FSR 3 Frame Generation Guide](https://gpuopen.com/fidelityfx-super-resolution-3/)
- `external/FidelityFX-SDK/Kits/FidelityFX/framegeneration/include/`
