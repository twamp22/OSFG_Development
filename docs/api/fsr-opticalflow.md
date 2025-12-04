# FSR Optical Flow API Reference

The `osfg_fsr_opticalflow` library provides a wrapper for AMD FidelityFX FSR 3 optical flow.

## Status

**Current Status**: DLL Available - Integration Pending

The FidelityFX SDK has been successfully built and the pre-compiled DLLs are available in `build/bin/Release/`:
- `amd_fidelityfx_framegeneration_dx12.dll`
- `amd_fidelityfx_loader_dx12.dll`
- `amd_fidelityfx_upscaler_dx12.dll`

However, the FidelityFX DLLs provide bundled frame generation (optical flow + interpolation together) rather than standalone optical flow. Integration options are documented below.

## Integration Options

### Option 1: Full Frame Generation (Recommended)

Use `amd_fidelityfx_framegeneration_dx12.dll` directly via the ffx_api.h interface. This provides:
- Optical flow + interpolation as a unified pipeline
- Highest quality results
- Requires restructuring OSFG to use FFX for both stages

### Option 2: Standalone Optical Flow

Build FidelityFX SDK from source with shader blob generation:
- Enables standalone optical flow API
- More complex integration
- Allows using OSFG's custom interpolation

### Current Approach

OSFG uses `SimpleOpticalFlow` (block-matching) by default:
- No external DLL dependencies
- Works on all D3D12 hardware
- Suitable for basic frame generation

## Headers

```cpp
#include "opticalflow/fsr_opticalflow.h"
```

## Namespace

All types are in the `OSFG` namespace.

## Structures

### FSROpticalFlowConfig

Configuration for FSR optical flow initialization.

```cpp
struct FSROpticalFlowConfig {
    uint32_t width = 1920;       // Input frame width
    uint32_t height = 1080;      // Input frame height
    bool enableTexture1D = false; // Use 1D textures if supported
};
```

### FSROpticalFlowStats

Runtime statistics.

```cpp
struct FSROpticalFlowStats {
    double lastDispatchTimeMs = 0.0;   // Last dispatch time
    double avgDispatchTimeMs = 0.0;    // Average dispatch time
    uint64_t framesProcessed = 0;      // Total frames processed
    size_t gpuMemoryUsageBytes = 0;    // GPU memory usage
};
```

## Classes

### FSROpticalFlow

Wrapper for AMD FidelityFX optical flow.

#### Static Methods

```cpp
static bool IsAvailable();
```

Check if FSR optical flow is available and functional.

**Returns**: `true` if available and fully implemented, `false` otherwise.

**Note**: Currently returns `false` as integration is pending.

```cpp
static bool IsDllPresent();
```

Check if FidelityFX DLL is present (may not be usable yet).

**Returns**: `true` if `amd_fidelityfx_framegeneration_dx12.dll` was found.

```cpp
static const std::wstring& GetDllPath();
```

Get path to loaded FidelityFX DLL (for diagnostics).

#### Constructor/Destructor

```cpp
FSROpticalFlow();
~FSROpticalFlow();
```

#### Initialization

```cpp
bool Initialize(ID3D12Device* device, const FSROpticalFlowConfig& config);
void Shutdown();
bool IsInitialized() const;
```

**Initialize** sets up the optical flow context with the given D3D12 device and configuration.

**Returns**: `true` on success, `false` on failure.

#### Dispatch

```cpp
bool Dispatch(ID3D12Resource* currentFrame,
              ID3D12GraphicsCommandList* commandList,
              bool reset = false);
```

Compute optical flow for the current frame.

**Parameters**:
- `currentFrame`: Input color texture
- `commandList`: Command list for GPU work
- `reset`: Set to `true` on scene cuts or camera discontinuities

**Returns**: `true` on success, `false` on failure.

#### Output Resources

```cpp
ID3D12Resource* GetMotionVectorTexture() const;
ID3D12Resource* GetSceneChangeTexture() const;
```

Get the computed motion vectors and scene change detection textures.

**Motion Vector Format**: `DXGI_FORMAT_R16G16_SINT` - motion in pixels

#### Dimensions

```cpp
uint32_t GetOpticalFlowWidth() const;
uint32_t GetOpticalFlowHeight() const;
```

Get the optical flow output dimensions (typically smaller than input).

#### Statistics

```cpp
const FSROpticalFlowStats& GetStats() const;
```

Get current statistics.

#### Error Handling

```cpp
const std::string& GetLastError() const;
```

Get the last error message.

## Usage Example

```cpp
#include "opticalflow/fsr_opticalflow.h"

// Check if DLL is present
if (OSFG::FSROpticalFlow::IsDllPresent()) {
    printf("FidelityFX DLL found at: %ls\n",
           OSFG::FSROpticalFlow::GetDllPath().c_str());
}

// Check availability (returns false until integration is complete)
if (!OSFG::FSROpticalFlow::IsAvailable()) {
    printf("FSR optical flow not available, using SimpleOpticalFlow\n");
    return;
}

OSFG::FSROpticalFlow opticalFlow;

OSFG::FSROpticalFlowConfig config;
config.width = 1920;
config.height = 1080;

if (!opticalFlow.Initialize(device, config)) {
    printf("Error: %s\n", opticalFlow.GetLastError().c_str());
    return;
}

// In render loop
opticalFlow.Dispatch(colorTexture, commandList, isSceneCut);

// Get results
ID3D12Resource* motionVectors = opticalFlow.GetMotionVectorTexture();
```

## Comparison with SimpleOpticalFlow

| Feature | SimpleOpticalFlow | FSROpticalFlow |
|---------|-------------------|----------------|
| Dependencies | None | FidelityFX DLLs |
| Quality | Basic block-matching | Advanced hierarchical |
| Performance | ~2-4ms | ~1-2ms (estimated) |
| Scene Change | Manual detection | Built-in SCD |
| Status | Fully implemented | DLL present, integration pending |

## FidelityFX SDK Build

The FSR sample was successfully built, confirming the SDK works:

```bash
# Build FidelityFX FSR sample
MSBuild.exe "external/FidelityFX-SDK/Samples/Upscalers/FidelityFX_FSR/dx12/FidelityFX_FSR_2022.sln" /p:Configuration=Release /p:Platform=x64
```

Build outputs:
- `FidelityFX_FSR.exe` - Sample application
- `Cauldron.lib` - Framework library
- DLLs copied to output directory

## See Also

- [Optical Flow API](opticalflow.md) - SimpleOpticalFlow documentation
- [Pipeline API](pipeline.md) - Dual-GPU pipeline
- [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)
