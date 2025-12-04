# OSFG Architecture

This document describes the system architecture, pipeline design, and data flow of OSFG.

## System Overview

OSFG operates as a post-process frame generation system that sits between the display output and the viewer. Unlike engine-integrated solutions (FSR 3, DLSS 3), OSFG captures frames after they're rendered, making it compatible with any application.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         APPLICATION LAYER                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                      │
│  │   Config    │  │   Hotkey    │  │   Stats     │                      │
│  │   Manager   │  │   Handler   │  │   Overlay   │                      │
│  └─────────────┘  └─────────────┘  └─────────────┘                      │
└─────────────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          CAPTURE LAYER                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                    DXGI Desktop Duplication                      │    │
│  │  • Acquires frames from Windows compositor                       │    │
│  │  • Provides D3D11 texture output                                 │    │
│  │  • Handles cursor capture (optional)                             │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         INTEROP LAYER                                    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                   D3D11 ↔ D3D12 Interop                          │    │
│  │  • Shares textures between API versions                          │    │
│  │  • Manages double-buffering for frame history                    │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       PROCESSING LAYER                                   │
│                                                                          │
│  ┌────────────────────────┐      ┌────────────────────────┐            │
│  │     Optical Flow       │      │   Frame Interpolation  │            │
│  │                        │      │                        │            │
│  │  • Luminance pyramid   │─────▶│  • Motion compensation │            │
│  │  • Block matching      │      │  • Bidirectional warp  │            │
│  │  • Motion vectors      │      │  • Hole filling        │            │
│  └────────────────────────┘      └────────────────────────┘            │
│                                            │                            │
└────────────────────────────────────────────│────────────────────────────┘
                                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       PRESENTATION LAYER                                 │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                    Frame Pacing & Display                        │    │
│  │  • Interleaves real and generated frames                         │    │
│  │  • VSync/VRR coordination                                        │    │
│  │  • Swap chain management                                         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

## Pipeline Architecture

### Single-GPU Mode

In single-GPU mode, all processing happens on the primary GPU:

```
Frame N-1 ──┐
            ├──▶ Optical Flow ──▶ Interpolation ──▶ Frame N-0.5
Frame N ────┘                                            │
    │                                                    │
    └──────────────────────────────────────────────────▶ Present
```

**Latency Budget (Single-GPU @ 1080p):**

| Stage | Target | Actual |
|-------|--------|--------|
| Capture | < 2ms | ~1-2ms |
| Interop | < 1ms | ~0.5ms |
| Optical Flow | < 3ms | ~2-3ms |
| Interpolation | < 4ms | ~3-4ms |
| Present | < 1ms | ~0.5ms |
| **Total** | **< 11ms** | **~7-10ms** |

### Dual-GPU Mode

In dual-GPU mode, frame generation is offloaded to a secondary GPU:

```
┌─────────────────────┐          ┌─────────────────────┐
│   GPU 0 (Primary)   │   PCIe   │  GPU 1 (Secondary)  │
│                     │          │                     │
│  Game renders here  │ ───────▶ │  Optical Flow       │
│  DXGI Capture       │          │  Interpolation      │
│                     │          │  Presentation       │
└─────────────────────┘          └─────────────────────┘
```

**Transfer Methods:**

1. **Cross-Adapter Heap** (Preferred)
   - Direct GPU-to-GPU via shared heap
   - Requires Windows 10 and compatible GPUs
   - ~1-2ms transfer time

2. **CPU Staging** (Fallback)
   - GPU 0 → CPU → GPU 1
   - Always works, higher latency
   - ~3-5ms transfer time

## Data Flow

### Frame Timing (2X Mode @ 60fps base)

```
Time (ms)    0      8.33    16.67   25.00   33.33
             │       │        │       │       │
Base Game    ├─N─────┼────────├─N+1───┼───────├─N+2──
             │       │        │       │       │
Output       ├─N─────├─N+0.5──├─N+1───├─N+1.5─├─N+2──
             │       │        │       │       │
Display      60fps base → 120fps output
```

### Memory Layout

```
┌─────────────────────────────────────────────────────┐
│                    Frame Buffer Pool                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ Frame N-1│  │ Frame N  │  │ Frame N+1│          │
│  │ (prev)   │  │ (current)│  │ (next)   │          │
│  └──────────┘  └──────────┘  └──────────┘          │
│                                                      │
│  ┌──────────────────────────────────────────┐      │
│  │           Motion Vector Buffer            │      │
│  │  (width/8 × height/8 × sizeof(int2))     │      │
│  └──────────────────────────────────────────┘      │
│                                                      │
│  ┌──────────────────────────────────────────┐      │
│  │         Interpolated Frame Buffer         │      │
│  └──────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────┘
```

## Module Responsibilities

### Capture (`osfg_capture`)

- Initialize DXGI Desktop Duplication
- Acquire frames with minimal latency
- Provide D3D11 texture handles
- Track capture statistics

### Interop (`osfg_interop`)

- Create D3D11On12 device wrapper
- Share textures between D3D11 and D3D12
- Manage frame history (previous/current)
- Handle synchronization

### Optical Flow (`osfg_simple_opticalflow`)

- Convert RGB to luminance
- Build image pyramid
- Perform block matching search
- Output motion vector field

### Interpolation (`osfg_interpolation`)

- Read motion vectors
- Warp previous frame forward
- Warp current frame backward
- Blend warped frames
- Fill disoccluded regions

### Presentation (`osfg_presentation`)

- Create output window
- Manage swap chain
- Handle frame pacing
- Coordinate with VSync/VRR

### Transfer (`osfg_transfer`)

- Enumerate GPUs
- Detect peer-to-peer capability
- Create cross-adapter resources
- Transfer frames between GPUs
- Synchronize GPU timelines

### Application (`osfg_app`)

- Load/save configuration
- Register global hotkeys
- Render statistics overlay
- Handle user input

## Threading Model

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Main Thread   │     │  Capture Thread │     │  Present Thread │
│                 │     │                 │     │                 │
│  • Window loop  │     │  • DXGI polling │     │  • Frame pacing │
│  • User input   │     │  • Frame queue  │     │  • Swap chain   │
│  • Config UI    │     │                 │     │  • VSync wait   │
└────────┬────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                       │
         └───────────────────────┴───────────────────────┘
                                 │
                    ┌────────────┴────────────┐
                    │      GPU Work Queue      │
                    │  • Optical flow compute  │
                    │  • Interpolation compute │
                    │  • Resource transitions  │
                    └─────────────────────────┘
```

## Synchronization

### GPU Synchronization

- **Fences**: Track GPU work completion
- **Barriers**: Transition resource states
- **Events**: Signal CPU from GPU

### Cross-Adapter Synchronization

```cpp
// Source GPU signals shared fence
sourceQueue->Signal(sharedFence, value);

// Destination GPU waits on shared fence
destQueue->Wait(sharedFence, value);
```

## Performance Considerations

### Bandwidth Requirements

| Resolution | Format | @ 60fps | @ 120fps |
|------------|--------|---------|----------|
| 1080p | BGRA | 498 MB/s | 996 MB/s |
| 1440p | BGRA | 885 MB/s | 1.77 GB/s |
| 4K | BGRA | 1.99 GB/s | 3.98 GB/s |

### GPU Memory Usage

| Component | 1080p | 1440p | 4K |
|-----------|-------|-------|-----|
| Frame buffers (×3) | 24 MB | 42 MB | 95 MB |
| Motion vectors | 0.5 MB | 0.9 MB | 2 MB |
| Intermediate buffers | 16 MB | 28 MB | 64 MB |
| **Total** | **~50 MB** | **~80 MB** | **~170 MB** |

## Future Architecture (Phase 2+)

### FSR 3 Integration

Replace simple optical flow with AMD's FidelityFX Optical Flow:
- Higher quality motion vectors
- Scene change detection
- Hardware-accelerated on RDNA2+

### Vulkan Backend

Alternative rendering backend for:
- Linux support
- Better multi-GPU control
- Cross-platform compatibility
