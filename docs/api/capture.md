# Capture Module API

The capture module (`osfg_capture`) provides DXGI Desktop Duplication functionality for low-latency frame acquisition.

## Header

```cpp
#include "capture/dxgi_capture.h"
```

## Namespace

```cpp
namespace osfg { ... }
```

## Classes

### DXGICapture

Main class for capturing frames from the desktop compositor.

#### Constructor/Destructor

```cpp
DXGICapture();
~DXGICapture();
```

#### Initialization

```cpp
// Initialize with default device
bool Initialize(const CaptureConfig& config = CaptureConfig{});

// Initialize with external D3D11 device (for interop)
bool Initialize(ID3D11Device* externalDevice, const CaptureConfig& config = CaptureConfig{});

// Shutdown and release resources
void Shutdown();
```

#### Frame Capture

```cpp
// Capture the next frame
// Returns true if a new frame was captured
bool CaptureFrame(CapturedFrame& outFrame);

// Release the current frame (must call before next capture)
void ReleaseFrame();
```

#### Accessors

```cpp
// Get capture statistics
const CaptureStats& GetStats() const;

// Reset statistics
void ResetStats();

// Get D3D11 device
ID3D11Device* GetDevice() const;

// Get D3D11 device context
ID3D11DeviceContext* GetContext() const;

// Get display dimensions
uint32_t GetWidth() const;
uint32_t GetHeight() const;

// Check initialization state
bool IsInitialized() const;

// Get last error message
const std::string& GetLastError() const;
```

## Structures

### CaptureConfig

Configuration for capture initialization.

```cpp
struct CaptureConfig {
    uint32_t outputIndex = 0;          // Monitor index (0 = primary)
    uint32_t adapterIndex = 0;         // GPU adapter index
    bool createStagingTexture = false; // Create CPU-readable staging
    uint32_t timeoutMs = 16;           // Frame acquisition timeout
};
```

### CapturedFrame

Data for a captured frame.

```cpp
struct CapturedFrame {
    ComPtr<ID3D11Texture2D> texture;   // Captured texture
    uint32_t width = 0;                // Frame width
    uint32_t height = 0;               // Frame height
    DXGI_FORMAT format;                // Pixel format
    uint64_t frameNumber = 0;          // Sequential frame number
    std::chrono::high_resolution_clock::time_point captureTime;
    bool isValid = false;              // True if frame is valid
};
```

### CaptureStats

Capture performance statistics.

```cpp
struct CaptureStats {
    uint64_t framesCapture = 0;        // Total frames captured
    uint64_t framesMissed = 0;         // Frames missed/dropped
    double avgCaptureTimeMs = 0.0;     // Average capture time
    double lastCaptureTimeMs = 0.0;    // Last capture time
    double minCaptureTimeMs = 0.0;     // Minimum capture time
    double maxCaptureTimeMs = 0.0;     // Maximum capture time
};
```

## Usage Example

```cpp
#include "capture/dxgi_capture.h"

using namespace osfg;

int main() {
    DXGICapture capture;

    // Configure capture
    CaptureConfig config;
    config.outputIndex = 0;     // Primary monitor
    config.timeoutMs = 0;       // Non-blocking

    // Initialize
    if (!capture.Initialize(config)) {
        printf("Error: %s\n", capture.GetLastError().c_str());
        return 1;
    }

    printf("Capturing at %dx%d\n", capture.GetWidth(), capture.GetHeight());

    // Capture loop
    CapturedFrame frame;
    while (running) {
        if (capture.CaptureFrame(frame)) {
            // Process frame.texture
            ProcessTexture(frame.texture.Get());

            // Must release before next capture
            capture.ReleaseFrame();
        }
    }

    // Print statistics
    const auto& stats = capture.GetStats();
    printf("Captured %llu frames, missed %llu\n",
           stats.framesCapture, stats.framesMissed);
    printf("Avg capture time: %.2f ms\n", stats.avgCaptureTimeMs);

    capture.Shutdown();
    return 0;
}
```

## Error Handling

Common errors returned by `GetLastError()`:

| Error | Cause | Solution |
|-------|-------|----------|
| "Desktop duplication not available" | Another app using it | Close OBS, Discord, etc. |
| "Access denied" | Insufficient permissions | Run as administrator |
| "Failed to get adapter N" | Invalid adapter index | Check available adapters |
| "Desktop duplication access lost" | Display mode changed | Re-initialize capture |

## Thread Safety

- `DXGICapture` is **not thread-safe**
- Call all methods from the same thread
- For multi-threaded use, protect with mutex or use one instance per thread

## Performance Tips

1. **Use timeout=0** for non-blocking capture in a dedicated thread
2. **Release frames immediately** after copying to allow OS tracking
3. **Avoid staging textures** unless CPU readback is required
4. **Monitor `framesMissed`** to detect capture bottlenecks
