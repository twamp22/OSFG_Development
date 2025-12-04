# Pipeline API Reference

The `osfg_pipeline` library provides the `DualGPUPipeline` class that orchestrates the complete dual-GPU frame generation workflow.

## Overview

The pipeline coordinates:
- Frame capture on the primary GPU
- Cross-adapter transfer to the secondary GPU
- Optical flow computation
- Frame interpolation
- Presentation with proper frame pacing

## Headers

```cpp
#include "pipeline/dual_gpu_pipeline.h"
```

## Namespace

All pipeline types are in the `osfg` namespace.

## Enumerations

### FrameMultiplier

```cpp
enum class FrameMultiplier {
    X2 = 2,     // 60 -> 120 fps
    X3 = 3,     // 60 -> 180 fps
    X4 = 4      // 60 -> 240 fps
};
```

## Structures

### DualGPUConfig

Configuration for the dual-GPU pipeline.

```cpp
struct DualGPUConfig {
    // GPU selection
    uint32_t primaryGPU = 0;        // Capture GPU (usually the gaming GPU)
    uint32_t secondaryGPU = 1;      // Frame generation GPU

    // Resolution
    uint32_t width = 1920;
    uint32_t height = 1080;

    // Frame generation
    FrameMultiplier multiplier = FrameMultiplier::X2;
    bool enableFrameGen = true;

    // Capture settings
    uint32_t captureMonitor = 0;
    uint32_t captureTimeoutMs = 0;  // 0 = non-blocking

    // Presentation
    bool vsync = true;
    bool borderlessWindow = true;
    const wchar_t* windowTitle = L"OSFG Dual-GPU Frame Generation";

    // Transfer settings
    bool preferPeerToPeer = true;
    uint32_t transferBufferCount = 3;

    // Optical flow
    uint32_t opticalFlowBlockSize = 8;
    uint32_t opticalFlowSearchRadius = 12;

    // Advanced
    bool enableOverlay = true;
    bool enableDebugOutput = false;
};
```

### PipelineStats

Runtime statistics from the pipeline.

```cpp
struct PipelineStats {
    // Frame counts
    uint64_t baseFamesCaptured = 0;
    uint64_t framesGenerated = 0;
    uint64_t framesPresented = 0;
    uint64_t framesDropped = 0;

    // Timing (milliseconds)
    double captureTimeMs = 0.0;
    double transferTimeMs = 0.0;
    double opticalFlowTimeMs = 0.0;
    double interpolationTimeMs = 0.0;
    double presentTimeMs = 0.0;
    double totalPipelineTimeMs = 0.0;

    // Frame rates
    double baseFPS = 0.0;
    double outputFPS = 0.0;

    // Transfer stats
    double transferThroughputMBps = 0.0;
    bool usingPeerToPeer = false;
};
```

## Classes

### DualGPUPipeline

Main pipeline orchestration class.

#### Constructor/Destructor

```cpp
DualGPUPipeline();
~DualGPUPipeline();
```

#### Initialization

```cpp
bool Initialize(const DualGPUConfig& config);
void Shutdown();
bool IsInitialized() const;
```

**Initialize** sets up all pipeline components:
- DXGI capture on primary GPU
- Cross-adapter transfer
- Optical flow and interpolation on secondary GPU
- Presentation window

Returns `true` on success, `false` on failure (check `GetLastError()`).

#### Pipeline Control

```cpp
bool Start();
void Stop();
bool IsRunning() const;
```

**Start** begins the pipeline. **Stop** halts processing.

#### Frame Processing

```cpp
bool ProcessFrame();
void Run();
```

**ProcessFrame** processes a single frame through all pipeline stages. Call this in a loop for manual control.

**Run** processes frames autonomously until `Stop()` is called or the window closes.

#### Runtime Configuration

```cpp
void SetFrameGenEnabled(bool enabled);
bool IsFrameGenEnabled() const;

void SetFrameMultiplier(FrameMultiplier multiplier);
FrameMultiplier GetFrameMultiplier() const;
```

Enable/disable frame generation and change multiplier at runtime.

#### Statistics

```cpp
const PipelineStats& GetStats() const;
void ResetStats();
```

Get current pipeline statistics or reset counters.

#### Error Handling

```cpp
const std::string& GetLastError() const;
```

Returns the last error message.

#### Callbacks

```cpp
using FrameCallback = std::function<void(uint64_t frameNumber, double frameTimeMs)>;
using ErrorCallback = std::function<void(const std::string& error)>;

void SetFrameCallback(FrameCallback callback);
void SetErrorCallback(ErrorCallback callback);
```

Set callbacks for frame completion and error notifications.

#### Window Access

```cpp
HWND GetWindowHandle() const;
bool IsWindowOpen() const;
```

Get the presentation window handle or check if it's still open.

## Usage Example

```cpp
#include "pipeline/dual_gpu_pipeline.h"
#include <cstdio>

using namespace osfg;

int main() {
    DualGPUPipeline pipeline;

    // Configure pipeline
    DualGPUConfig config;
    config.primaryGPU = 0;      // Gaming GPU
    config.secondaryGPU = 1;    // Frame gen GPU
    config.multiplier = FrameMultiplier::X2;
    config.vsync = true;

    // Set error callback
    pipeline.SetErrorCallback([](const std::string& error) {
        printf("Error: %s\n", error.c_str());
    });

    // Initialize
    if (!pipeline.Initialize(config)) {
        printf("Init failed: %s\n", pipeline.GetLastError().c_str());
        return 1;
    }

    // Start pipeline
    if (!pipeline.Start()) {
        printf("Start failed: %s\n", pipeline.GetLastError().c_str());
        return 1;
    }

    // Manual processing loop
    while (pipeline.IsRunning() && pipeline.IsWindowOpen()) {
        // Process window messages
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Process frame
        pipeline.ProcessFrame();

        // Print stats
        const auto& stats = pipeline.GetStats();
        printf("\rFPS: %.1f -> %.1f", stats.baseFPS, stats.outputFPS);
    }

    // Cleanup
    pipeline.Stop();
    pipeline.Shutdown();

    return 0;
}
```

## Pipeline Stages

The pipeline executes these stages per frame:

1. **Capture**: DXGI Desktop Duplication acquires frame from primary GPU
2. **Transfer**: Frame copied to secondary GPU (via cross-adapter heap or staging)
3. **Optical Flow**: Motion vectors computed from current and previous frames
4. **Interpolation**: Generated frames created using motion compensation
5. **Presentation**: Frames presented with proper pacing for target frame rate

## Thread Safety

- `GetStats()` is thread-safe
- `SetFrameGenEnabled()` and `SetFrameMultiplier()` are thread-safe
- Other methods should be called from the main thread

## Dependencies

- `osfg_capture` - Frame capture
- `osfg_transfer` - Cross-adapter transfer
- `osfg_simple_opticalflow` - Motion estimation
- `osfg_interpolation` - Frame generation
- `osfg_presentation` - Display output

## See Also

- [Transfer API](transfer.md) - Inter-GPU transfer details
- [Optical Flow API](opticalflow.md) - Motion estimation
- [Interpolation API](interpolation.md) - Frame generation
