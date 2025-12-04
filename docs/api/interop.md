# Interop Module API

The interop module (`osfg_interop`) handles texture sharing between D3D11 (capture) and D3D12 (compute) APIs.

## Header

```cpp
#include "interop/d3d11_d3d12_interop.h"
```

## Namespace

```cpp
namespace OSFG { ... }
```

## Classes

### D3D11D3D12Interop

Bridges D3D11 textures from DXGI capture to D3D12 for GPU compute.

#### Constructor/Destructor

```cpp
D3D11D3D12Interop();
~D3D11D3D12Interop();
```

#### Initialization

```cpp
// Initialize with existing D3D12 device
bool Initialize(ID3D12Device* d3d12Device,
                ID3D12CommandQueue* commandQueue,
                const InteropConfig& config);

// Shutdown and release resources
void Shutdown();

// Check initialization state
bool IsInitialized() const;
```

#### Texture Transfer

```cpp
// Copy from D3D11 texture using D3D11On12 (same device)
bool CopyFromD3D11(ID3D11Texture2D* srcTexture);

// Copy from external D3D11 device via CPU staging
bool CopyFromD3D11Staged(ID3D11Device* srcDevice,
                          ID3D11DeviceContext* srcContext,
                          ID3D11Texture2D* srcTexture);

// Swap buffers (current becomes previous)
void SwapBuffers();
```

#### Resource Access

```cpp
// Get D3D12 textures for compute
ID3D12Resource* GetCurrentFrameD3D12() const;
ID3D12Resource* GetPreviousFrameD3D12() const;

// Get internal D3D11 device
ID3D11Device* GetD3D11Device() const;
ID3D11DeviceContext* GetD3D11Context() const;

// Get frame count
uint64_t GetFrameCount() const;

// Get last error
const std::string& GetLastError() const;
```

## Structures

### InteropConfig

```cpp
struct InteropConfig {
    uint32_t width = 1920;                          // Frame width
    uint32_t height = 1080;                         // Frame height
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM; // Pixel format
    uint32_t bufferCount = 2;                       // Double buffer
};
```

## Architecture

The interop module uses D3D11On12 to share textures:

```
┌─────────────────────────────────────────────────────────────┐
│                    D3D12 Device (Compute)                   │
│  ┌─────────────┐    ┌─────────────┐                        │
│  │ D3D12       │    │ D3D12       │                        │
│  │ Texture 0   │    │ Texture 1   │                        │
│  │ (current)   │    │ (previous)  │                        │
│  └──────┬──────┘    └──────┬──────┘                        │
│         │                  │                                │
│         │   D3D11On12 Wrapping                             │
│         │                  │                                │
│  ┌──────┴──────┐    ┌──────┴──────┐                        │
│  │ D3D11       │    │ D3D11       │                        │
│  │ Wrapped 0   │    │ Wrapped 1   │                        │
│  └──────┬──────┘    └─────────────┘                        │
│         │                                                   │
└─────────│───────────────────────────────────────────────────┘
          │
          │  CopyFromD3D11() / CopyFromD3D11Staged()
          │
┌─────────┴───────────────────────────────────────────────────┐
│  ┌─────────────┐                                            │
│  │ DXGI Capture│    D3D11 Device (Capture)                 │
│  │ Texture     │                                            │
│  └─────────────┘                                            │
└─────────────────────────────────────────────────────────────┘
```

## Usage Example

```cpp
#include "interop/d3d11_d3d12_interop.h"
#include "capture/dxgi_capture.h"

using namespace OSFG;

// Initialize D3D12 for compute
ID3D12Device* d3d12Device = CreateD3D12Device();
ID3D12CommandQueue* commandQueue = CreateCommandQueue(d3d12Device);

// Initialize interop
D3D11D3D12Interop interop;
InteropConfig config;
config.width = 1920;
config.height = 1080;

if (!interop.Initialize(d3d12Device, commandQueue, config)) {
    printf("Error: %s\n", interop.GetLastError().c_str());
    return;
}

// Initialize capture
osfg::DXGICapture capture;
capture.Initialize();

// Main loop
osfg::CapturedFrame frame;
while (running) {
    if (capture.CaptureFrame(frame)) {
        // Transfer to D3D12
        interop.CopyFromD3D11Staged(
            capture.GetDevice(),
            capture.GetContext(),
            frame.texture.Get()
        );

        // Get D3D12 textures for compute
        ID3D12Resource* current = interop.GetCurrentFrameD3D12();
        ID3D12Resource* previous = interop.GetPreviousFrameD3D12();

        // Run optical flow, interpolation, etc.
        ProcessFrames(current, previous);

        // Swap for next frame
        interop.SwapBuffers();

        capture.ReleaseFrame();
    }
}

interop.Shutdown();
```

## Transfer Methods

### CopyFromD3D11 (D3D11On12)

Uses D3D11On12 device wrapping for zero-copy sharing when possible.

**Requirements:**
- Source texture must be on compatible D3D11 device
- Same adapter

**Performance:** Near-instant (pointer sharing)

### CopyFromD3D11Staged (CPU Staging)

Falls back to CPU staging when devices are incompatible.

**Flow:**
1. Map source texture to CPU memory
2. Copy to mapped upload buffer
3. Upload to D3D12 texture

**Performance:** ~1-3ms depending on resolution

## Double Buffering

The interop maintains two textures:
- **Current**: Most recently captured frame
- **Previous**: Frame from before current

This enables optical flow which requires two consecutive frames.

```cpp
// Frame N captured
interop.CopyFromD3D11Staged(...);  // Goes to "current"
interop.SwapBuffers();              // Current → Previous

// Frame N+1 captured
interop.CopyFromD3D11Staged(...);  // Goes to "current"

// Now:
// GetCurrentFrameD3D12()  → Frame N+1
// GetPreviousFrameD3D12() → Frame N
```

## Resource States

| Resource | State After Copy | Expected Use |
|----------|------------------|--------------|
| Current | PIXEL_SHADER_RESOURCE | Compute input |
| Previous | PIXEL_SHADER_RESOURCE | Compute input |

Transition states as needed for your compute pipeline.

## Thread Safety

- `D3D11D3D12Interop` is **not thread-safe**
- Capture and interop should run on the same thread
- Use fences for D3D12 synchronization

## Performance Tips

1. **Prefer CopyFromD3D11** when devices are compatible
2. **Use double buffering** to avoid waiting for copies
3. **Map upload buffers persistently** to reduce overhead
4. **Align row pitch** to D3D12 requirements (256 bytes)
