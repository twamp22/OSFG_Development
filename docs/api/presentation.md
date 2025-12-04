# Presentation Module API

The presentation module (`osfg_presentation`) handles frame display and pacing using DirectX 12.

## Header

```cpp
#include "presentation/simple_presenter.h"
```

## Namespace

```cpp
namespace OSFG { ... }
```

## Classes

### SimplePresenter

Displays frames in a window with proper pacing.

#### Constructor/Destructor

```cpp
SimplePresenter();
~SimplePresenter();
```

#### Initialization

```cpp
// Initialize with D3D12 device and command queue
bool Initialize(ID3D12Device* device,
                ID3D12CommandQueue* commandQueue,
                const PresenterConfig& config);

// Shutdown and release resources
void Shutdown();

// Check initialization state
bool IsInitialized() const;
```

#### Window Management

```cpp
// Check if window is still open
bool IsWindowOpen() const;

// Process window messages (call in main loop)
bool ProcessMessages();

// Get window handle
HWND GetHWND() const;

// Get dimensions
uint32_t GetWidth() const;
uint32_t GetHeight() const;
```

#### Frame Presentation

```cpp
// Present a frame
// sourceTexture: Texture to display (PIXEL_SHADER_RESOURCE state)
// commandList: Command list for copy commands
bool Present(ID3D12Resource* sourceTexture,
             ID3D12GraphicsCommandList* commandList);

// Get current back buffer
ID3D12Resource* GetCurrentBackBuffer();
uint32_t GetCurrentBackBufferIndex() const;
```

#### Statistics

```cpp
// Get presentation statistics
const PresenterStats& GetStats() const;

// Get last error
const std::string& GetLastError() const;
```

## Structures

### PresenterConfig

```cpp
struct PresenterConfig {
    uint32_t width = 1920;              // Window width
    uint32_t height = 1080;             // Window height
    uint32_t bufferCount = 2;           // Swap chain buffer count
    bool vsync = true;                  // Enable VSync
    bool windowed = true;               // Windowed mode
    const wchar_t* windowTitle = L"OSFG Frame Generation";
};
```

### PresenterStats

```cpp
struct PresenterStats {
    uint64_t framesPresented = 0;       // Total frames presented
    double lastPresentTimeMs = 0.0;     // Last present time
    double avgPresentTimeMs = 0.0;      // Average present time
    double fps = 0.0;                   // Current FPS
};
```

## Usage Example

```cpp
#include "presentation/simple_presenter.h"

using namespace OSFG;

SimplePresenter presenter;

// Configure
PresenterConfig config;
config.width = 1920;
config.height = 1080;
config.vsync = true;
config.bufferCount = 2;
config.windowTitle = L"My Frame Gen App";

// Initialize
if (!presenter.Initialize(device, commandQueue, config)) {
    printf("Error: %s\n", presenter.GetLastError().c_str());
    return;
}

// Main loop
while (presenter.IsWindowOpen()) {
    // Process window messages
    if (!presenter.ProcessMessages()) {
        break;
    }

    // Get frame to present
    ID3D12Resource* frame = GetNextFrame();

    // Present
    presenter.Present(frame, commandList);
}

// Statistics
const auto& stats = presenter.GetStats();
printf("Presented %llu frames at %.1f FPS\n",
       stats.framesPresented, stats.fps);

presenter.Shutdown();
```

## Frame Pacing

### VSync Modes

| Mode | Behavior | Use Case |
|------|----------|----------|
| VSync On | Wait for vertical blank | Tear-free, fixed refresh |
| VSync Off | Present immediately | Lowest latency, may tear |

### Frame Timing (2X @ 60fps → 120fps)

```
Display Refresh    │  │  │  │  │  │  │  │
(120 Hz)          0  8  16 24 32 40 48 56 ms

Base Frame         N─────────N+1────────N+2
(60 fps)          │         │          │

Generated Frame      N+0.5     N+1.5
(interpolated)       │         │

Output             N  N+0.5  N+1 N+1.5 N+2
(120 fps)          │    │     │    │    │
```

### Pacing Implementation

```cpp
// Simple pacing for 2X mode
void PresentWithPacing(ID3D12Resource* realFrame,
                       ID3D12Resource* genFrame) {
    // Present real frame
    presenter.Present(realFrame, cmdList);

    // Wait half frame time
    Sleep(targetFrameTimeMs / 2);

    // Present generated frame
    presenter.Present(genFrame, cmdList);
}
```

## VRR/Adaptive Sync Support

For Variable Refresh Rate displays:

```cpp
// Check for VRR support
IDXGIOutput6* output6 = nullptr;
if (SUCCEEDED(output->QueryInterface(&output6))) {
    DXGI_OUTPUT_DESC1 desc;
    output6->GetDesc1(&desc);

    if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
        // VRR may be supported
    }
}

// Use DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING for VRR
DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

// Present with tearing allowed
swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
```

## Window Handling

### Window Procedure

The presenter creates a window with these default behaviors:
- **Close**: Sets window closed flag
- **Resize**: Updates swap chain (if supported)
- **Key Down**: Escape key closes window

### Custom Window

To use an existing window:

```cpp
// Create presenter with external HWND
bool InitializeWithHWND(ID3D12Device* device,
                        ID3D12CommandQueue* commandQueue,
                        HWND existingWindow,
                        const PresenterConfig& config);
```

## Performance Characteristics

| Resolution | Present Time | Frame Budget |
|------------|--------------|--------------|
| 1080p | < 1ms | 8.3ms @ 120fps |
| 1440p | < 1ms | 8.3ms @ 120fps |
| 4K | ~1ms | 8.3ms @ 120fps |

## Thread Safety

- `SimplePresenter` is **not thread-safe**
- Call all methods from the main thread
- Window message processing must be on the window's thread

## Error Handling

Common errors:

| Error | Cause | Solution |
|-------|-------|----------|
| "Failed to create swap chain" | Display mode issue | Check resolution/format |
| "Device removed" | GPU reset/driver crash | Recreate device and resources |
| "Window closed" | User closed window | Exit application cleanly |

## Best Practices

1. **Always check `IsWindowOpen()`** before presenting
2. **Call `ProcessMessages()`** every frame to keep window responsive
3. **Use VSync** to avoid tearing unless latency is critical
4. **Match swap chain buffer count** to frame generation mode (2 for 2X, 3 for 3X)
