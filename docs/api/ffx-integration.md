# FFX Integration API Reference

This document covers the FidelityFX SDK integration modules in OSFG.

## Overview

OSFG provides two FFX integration modules:
- `osfg_ffx_loader` - Dynamic loader for FidelityFX DLLs
- `osfg_ffx_framegen` - Frame generation wrapper

## FFXLoader Class

Dynamically loads FidelityFX SDK DLLs and resolves API functions.

### Header
```cpp
#include "ffx/ffx_loader.h"
```

### Static Methods

#### IsAvailable
```cpp
static bool IsAvailable();
```
Checks if FidelityFX DLLs are present in the application directory.

**Returns:** `true` if DLLs are available, `false` otherwise.

### Instance Methods

#### Instance
```cpp
static FFXLoader& Instance();
```
Gets the singleton instance of the loader.

#### Load
```cpp
bool Load();
```
Loads the FidelityFX DLLs and resolves function pointers.

**Returns:** `true` on success, `false` on failure.

#### Unload
```cpp
void Unload();
```
Unloads all DLLs and clears function pointers.

#### IsLoaded
```cpp
bool IsLoaded() const;
```
Checks if DLLs are currently loaded.

#### GetLastError
```cpp
const std::string& GetLastError() const;
```
Gets the last error message.

### Function Pointers

After successful `Load()`, the following function pointers are available:
- `CreateContext` - Create FFX context
- `DestroyContext` - Destroy FFX context
- `Configure` - Configure FFX context
- `Query` - Query FFX context
- `Dispatch` - Dispatch FFX operations

### Usage Example
```cpp
#include "ffx/ffx_loader.h"

// Check availability
if (!OSFG::FFXLoader::IsAvailable()) {
    printf("FidelityFX DLLs not found\n");
    return;
}

// Load
OSFG::FFXLoader& loader = OSFG::FFXLoader::Instance();
if (!loader.Load()) {
    printf("Failed to load: %s\n", loader.GetLastError().c_str());
    return;
}

// Use function pointers
if (loader.CreateContext) {
    // Create FFX context...
}
```

---

## FFXFrameGeneration Class

Wraps the FidelityFX Frame Generation API for easy integration.

### Header
```cpp
#include "ffx/ffx_framegen.h"
```

### Configuration Structures

#### FFXFrameGenConfig
```cpp
struct FFXFrameGenConfig {
    uint32_t displayWidth = 1920;
    uint32_t displayHeight = 1080;
    uint32_t backBufferCount = 3;
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool enableHDR = false;
    bool enableAsyncCompute = true;
    bool vsync = false;
};
```

#### FFXFrameGenStats
```cpp
struct FFXFrameGenStats {
    uint64_t framesGenerated = 0;
    uint64_t framesPresented = 0;
    float averageFrameTimeMs = 0.0f;
    float lastFrameTimeMs = 0.0f;
    uint64_t gpuMemoryUsageBytes = 0;
};
```

### Methods

#### Initialize (with HWND)
```cpp
bool Initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    IDXGIFactory4* dxgiFactory,
    HWND hwnd,
    const FFXFrameGenConfig& config
);
```
Creates a new swap chain with FFX frame generation enabled.

#### InitializeWithSwapChain
```cpp
bool InitializeWithSwapChain(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    IDXGISwapChain4* existingSwapChain
);
```
Wraps an existing swap chain for frame generation.

#### Shutdown
```cpp
void Shutdown();
```
Releases all resources.

#### Configure
```cpp
bool Configure(const FFXFrameGenConfig& config);
```
Updates configuration parameters.

#### SetEnabled
```cpp
bool SetEnabled(bool enabled);
```
Enables or disables frame generation at runtime.

#### Present
```cpp
bool Present(uint32_t syncInterval = 0, uint32_t flags = 0);
```
Presents the frame (replaces normal swap chain Present).

#### WaitForPendingPresents
```cpp
void WaitForPendingPresents();
```
Waits for all queued presents to complete.

#### GetSwapChain
```cpp
IDXGISwapChain4* GetSwapChain() const;
```
Gets the FFX-wrapped swap chain.

#### GetStats
```cpp
const FFXFrameGenStats& GetStats() const;
```
Gets current statistics.

### Usage Example
```cpp
#include "ffx/ffx_framegen.h"

// Create D3D12 device and command queue first...

OSFG::FFXFrameGeneration ffxGen;

OSFG::FFXFrameGenConfig config;
config.displayWidth = 1920;
config.displayHeight = 1080;
config.vsync = false;

if (!ffxGen.Initialize(device, commandQueue, factory, hwnd, config)) {
    printf("FFX init failed: %s\n", ffxGen.GetLastError().c_str());
    return;
}

// Render loop
while (running) {
    // Render to back buffer...

    // Present with FFX frame generation
    ffxGen.Present(0, DXGI_PRESENT_ALLOW_TEARING);

    // Get stats
    const auto& stats = ffxGen.GetStats();
    printf("Frame time: %.2f ms\n", stats.lastFrameTimeMs);
}

ffxGen.Shutdown();
```

---

## Pipeline Backend Selection

The `DualGPUPipeline` supports backend selection via the `FrameGenBackend` enum.

### Enum
```cpp
enum class FrameGenBackend {
    Native,     // SimpleOpticalFlow + OSFG Interpolation
    FidelityFX, // AMD FidelityFX Frame Generation
    Auto        // Automatically select best available
};
```

### Configuration
```cpp
DualGPUConfig config;
config.backend = FrameGenBackend::Auto;
```

### Checking Availability
```cpp
// Static check
bool available = DualGPUPipeline::IsFidelityFXAvailable();

// After initialization
FrameGenBackend active = pipeline.GetActiveBackend();
```

---

## Required DLLs

Place these DLLs in the application directory:
- `amd_fidelityfx_loader_dx12.dll`
- `amd_fidelityfx_framegeneration_dx12.dll`
- `amd_fidelityfx_upscaler_dx12.dll` (optional)

DLLs are automatically copied from `external/FidelityFX-SDK/Kits/FidelityFX/signedbin/` during build.

## See Also

- [FidelityFX Integration Design](../fidelityfx-integration-design.md)
- [Architecture Overview](../architecture.md)
- [Configuration Guide](../configuration.md)
