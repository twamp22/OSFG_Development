# Open Source Frame Generation (OSFG)
## Development Research & Planning Document

**Project Codename:** OSFG (Open Source Frame Generation)  
**Author:** Thomas / GydroGames  
**Created:** December 2025  
**Status:** Research & Planning Phase

---

## Table of Contents

1. [Project Vision](#project-vision)
2. [Technical Goals](#technical-goals)
3. [Architecture Overview](#architecture-overview)
4. [Component Deep Dive](#component-deep-dive)
5. [Open Source Resources](#open-source-resources)
6. [Development Phases](#development-phases)
7. [Research Questions](#research-questions)
8. [Testing Methodology](#testing-methodology)
9. [Known Challenges](#known-challenges)
10. [References & Resources](#references--resources)

---

## Project Vision

Build an open source, dual-GPU frame generation system that:
- Captures frames from any running application without engine integration
- Generates interpolated frames using optical flow analysis
- Offloads frame generation to a secondary GPU for zero-overhead operation
- Provides a universal solution similar to Lossless Scaling but fully open source

### Why This Matters

Current frame generation solutions are either:
- **Proprietary** (Lossless Scaling, DLSS 3)
- **Engine-integrated** (FSR 3 in-game, requiring developer support)
- **Hardware-locked** (DLSS 3 requires RTX 40-series)

An open source, post-process solution enables:
- Repurposing older GPUs as dedicated frame generators
- Community-driven improvements and ports
- Educational value for understanding real-time graphics pipelines
- Platform flexibility (Windows, Linux, potentially others)

---

## Technical Goals

### Primary Objectives

| Goal | Target | Priority |
|------|--------|----------|
| Frame capture latency | < 5ms | Critical |
| Frame generation quality | Comparable to FSR 3 | High |
| Dual-GPU transfer overhead | < 3-5ms | High |
| CPU overhead | < 5% single core | Medium |
| Memory footprint | < 500MB VRAM per GPU | Medium |

### Supported Configurations

**Phase 1 (MVP):**
- Windows 10/11
- Vulkan-capable GPUs (NVIDIA, AMD, Intel)
- Single monitor setup
- Borderless windowed games

**Phase 2:**
- Linux support via native Vulkan
- Multi-monitor configurations
- HDR passthrough

**Phase 3:**
- Exclusive fullscreen capture (hook-based)
- Adaptive frame generation (variable multiplier)
- Custom ML models

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        APPLICATION LAYER                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                 │
│  │   Config    │  │  Hotkey     │  │   Overlay   │                 │
│  │   Manager   │  │  Handler    │  │   (Stats)   │                 │
│  └─────────────┘  └─────────────┘  └─────────────┘                 │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         CAPTURE LAYER                               │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Frame Capture Engine                      │   │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐               │   │
│  │  │   DXGI    │  │    WGC    │  │  Hook-    │               │   │
│  │  │  Desktop  │  │  Windows  │  │  Based    │               │   │
│  │  │   Dup     │  │  Graphics │  │ (Future)  │               │   │
│  │  └───────────┘  └───────────┘  └───────────┘               │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      PROCESSING LAYER                               │
│                                                                     │
│  ┌──────────────────────┐      ┌──────────────────────┐           │
│  │     GPU 0 (Render)   │      │   GPU 1 (FrameGen)   │           │
│  │                      │      │                      │           │
│  │  • Game renders here │ PCIe │  • Optical Flow      │           │
│  │  • Frame captured    │ ───► │  • Frame Interp      │           │
│  │                      │      │  • Presentation      │           │
│  └──────────────────────┘      └──────────────────────┘           │
│                                          │                         │
│                                          ▼                         │
│                              ┌──────────────────────┐             │
│                              │   Frame Pacing &     │             │
│                              │   VSync Management   │             │
│                              └──────────────────────┘             │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       PRESENTATION LAYER                            │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Display Output                            │   │
│  │  • Real frames + Generated frames interleaved               │   │
│  │  • VRR/G-Sync/FreeSync coordination                         │   │
│  │  • Tear-line prevention                                      │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### Data Flow (Dual-GPU Mode)

```
Game (GPU 0)          Transfer           FrameGen (GPU 1)        Display
    │                    │                     │                    │
    │ Frame N            │                     │                    │
    ├───────────────────►│                     │                    │
    │                    ├────────────────────►│                    │
    │                    │                     │ Store Frame N      │
    │                    │                     │                    │
    │ Frame N+1          │                     │                    │
    ├───────────────────►│                     │                    │
    │                    ├────────────────────►│                    │
    │                    │                     │ Optical Flow       │
    │                    │                     │ (N vs N+1)         │
    │                    │                     │                    │
    │                    │                     │ Generate N+0.5     │
    │                    │                     │                    │
    │                    │                     │ Present N ─────────►
    │                    │                     │ Present N+0.5 ─────►
    │                    │                     │ (Paced)            │
    │                    │                     │                    │
```

---

## Component Deep Dive

### 1. Frame Capture Engine

**Purpose:** Acquire frames from the desktop compositor or game window with minimal latency.

#### DXGI Desktop Duplication (Primary Method)

```cpp
// Core API flow
IDXGIOutputDuplication::AcquireNextFrame()  // Get frame
IDXGIOutputDuplication::ReleaseFrame()      // Release for next

// Key considerations:
// - Only returns frames when content changes
// - Must poll rapidly, not block
// - Release frame ASAP after copy to allow OS to track updates
```

**Research Items:**
- [ ] Measure baseline latency of DXGI capture at various resolutions
- [ ] Test frame accumulation behavior under high GPU load
- [ ] Evaluate GPU-to-GPU texture sharing without CPU roundtrip

**Code References:**
- Microsoft DXGI Desktop Duplication sample
- D3DShot (Python): `github.com/SerpentAI/D3DShot`
- win_desktop_duplication (Rust): `lib.rs/crates/win_desktop_duplication`
- Looking Glass DXGI implementation (low-latency focus)

#### Windows Graphics Capture (Secondary Method)

```cpp
// WinRT API - higher level, more robust
Windows::Graphics::Capture::GraphicsCaptureSession
Windows::Graphics::Capture::Direct3D11CaptureFramePool
```

**Pros:** Better window-level capture, handles occlusion  
**Cons:** Slightly higher latency, Windows 10 1903+ only

**Research Items:**
- [ ] Compare latency vs DXGI in controlled tests
- [ ] Test behavior with DRM-protected content
- [ ] Evaluate frame pool sizing for optimal throughput

---

### 2. Inter-GPU Transfer

**Purpose:** Move captured frames from GPU 0 to GPU 1 efficiently.

#### Vulkan Approach (Recommended)

```cpp
// Vulkan provides explicit multi-GPU control
VkPhysicalDeviceGroupProperties  // Enumerate GPU groups
VkDeviceGroupDeviceCreateInfo    // Create multi-device logical device

// Memory transfer options:
// 1. Peer-to-peer (if supported)
// 2. Host-visible staging buffer
// 3. External memory (platform-specific)
```

**Critical Constraints:**
- PCIe 3.0 x4 minimum bandwidth: ~4 GB/s
- 1080p RGBA @ 60fps = ~500 MB/s (manageable)
- 4K RGBA @ 60fps = ~2 GB/s (needs x8 or compression)

**Research Items:**
- [ ] Benchmark PCIe transfer rates on target hardware
- [ ] Evaluate peer-to-peer capability across GPU vendors
- [ ] Test async transfer overlapped with frame generation
- [ ] Investigate compression (BC7, etc.) for bandwidth reduction

#### DirectX 12 Alternative

```cpp
// D3D12 cross-adapter resource sharing
ID3D12Device::OpenSharedHandle()
D3D12_CROSS_ADAPTER_ROW_MAJOR_TEXTURE
```

**Research Items:**
- [ ] Compare D3D12 vs Vulkan transfer performance
- [ ] Test cross-vendor compatibility (NVIDIA ↔ AMD)

---

### 3. Optical Flow Engine

**Purpose:** Estimate per-pixel or per-block motion between consecutive frames.

#### FSR 3 Optical Flow (MIT License)

The AMD FidelityFX Optical Flow algorithm:

```
Algorithm Overview:
1. Build luminance pyramid from input frame
2. Detect scene changes via histogram comparison
3. Iterative motion search (7 iterations):
   - Search pass: Find best match in 24x24 window
   - Filter pass: 3x3 median filter to remove outliers
   - Upscale pass: Prepare 2x resolution for next iteration
4. Output 8x8 block motion vectors
```

**Key Parameters:**
- Block size: 8x8 pixels
- Search window: 24x24 pixels (8px radius)
- Matching: Sum of Absolute Differences (SAD)
- Pyramid levels: 7 iterations

**Research Items:**
- [ ] Extract and compile FSR 3 optical flow shaders standalone
- [ ] Benchmark compute performance on various GPUs
- [ ] Test accuracy on high-motion game footage
- [ ] Evaluate scene change detection reliability

#### Alternative: OpenCV CUDA Optical Flow

```cpp
cv::cuda::FarnebackOpticalFlow
cv::cuda::BroxOpticalFlow
cv::cuda::OpticalFlowDual_TVL1
```

**Pros:** Well-documented, flexible  
**Cons:** May not be optimized for real-time game content

---

### 4. Frame Interpolation Engine

**Purpose:** Generate intermediate frames using optical flow and source frames.

#### FSR 3 Frame Interpolation (MIT License)

```
Required Inputs:
- Frame N (current backbuffer)
- Frame N-1 (previous backbuffer) 
- Optical flow vectors
- Scene change flag
- Delta time

Output:
- Interpolated frame at t=0.5 between N-1 and N
```

**Algorithm Stages:**
1. Setup pass - Clear internal buffers
2. Reconstruct previous depth (from optical flow)
3. Game motion vector field processing
4. Optical flow vector field integration
5. Disocclusion mask generation
6. Final interpolation blend

**Research Items:**
- [ ] Port FSR 3 frame interpolation to standalone Vulkan
- [ ] Test without engine motion vectors (optical-flow-only mode)
- [ ] Evaluate quality vs Lossless Scaling
- [ ] Profile memory usage and optimize

#### Handling UI/HUD

Frame generation typically produces artifacts on UI elements. Options:

1. **UI Callback** - Render UI separately per presented frame
2. **UI Texture** - Composite pre-rendered UI layer
3. **HUD-less Detection** - Compare frames to detect static UI regions

**Research Items:**
- [ ] Implement UI detection via frame differencing
- [ ] Test callback approach latency impact
- [ ] Evaluate machine learning UI segmentation

---

### 5. Frame Pacing & Presentation

**Purpose:** Present real and generated frames at correct intervals.

#### Pacing Logic

```
Target: 120 Hz display, 60 fps base game

Timeline (ms):
0.00  - Present Frame N (real)
8.33  - Present Frame N+0.5 (generated)
16.67 - Present Frame N+1 (real)
25.00 - Present Frame N+1.5 (generated)
...
```

**Implementation:**
- Dedicated pacing thread with high-resolution timer
- GPU fence synchronization for completion detection
- VRR integration for variable base framerates

**Research Items:**
- [ ] Implement frame pacing with sub-millisecond precision
- [ ] Test VRR/G-Sync/FreeSync compatibility
- [ ] Handle dropped frames gracefully
- [ ] Profile thread synchronization overhead

---

## Open Source Resources

### Core Dependencies

| Component | License | Source |
|-----------|---------|--------|
| AMD FidelityFX SDK | MIT | `github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK` |
| Vulkan SDK | Apache 2.0 | `vulkan.lunarg.com` |
| DXVK | zlib | `github.com/doitsujin/dxvk` |
| SDL3 | zlib | `github.com/libsdl-org/SDL` |

### Reference Projects

| Project | Relevance |
|---------|-----------|
| lsfg-vk | Vulkan layer injection, shader porting methodology |
| Looking Glass | Low-latency DXGI capture, GPU memory sharing |
| OBS Studio | DXGI/WGC capture implementations |
| mpv (with interpolation) | Frame interpolation for video |
| SVP (architecture reference) | Real-time frame interpolation pipeline |

### FSR 3 Components to Extract

```
FidelityFX-SDK/
├── sdk/src/components/
│   ├── opticalflow/          ← Optical flow shaders & pipeline
│   ├── frameinterpolation/   ← Frame generation shaders
│   └── fsr3/                 ← Combined FSR3 integration
├── sdk/include/
│   └── FidelityFX/host/      ← API headers
└── samples/fsr/              ← Reference implementation
```

---

## Development Phases

### Phase 0: Foundation Research (Current)
**Duration:** 2-4 weeks

- [x] Document Lossless Scaling architecture
- [x] Identify open source components
- [x] Create development plan
- [ ] Set up development environment
- [ ] Clone and build FSR 3 SDK samples
- [ ] Benchmark baseline capture methods

**Deliverable:** Working FSR 3 sample, DXGI capture prototype

---

### Phase 1: Single-GPU Proof of Concept
**Duration:** 4-6 weeks

**Milestone 1.1: Frame Capture**
- [ ] Implement DXGI Desktop Duplication wrapper
- [ ] Add frame timing instrumentation
- [ ] Create test harness with synthetic content
- [ ] Measure and optimize latency

**Milestone 1.2: Optical Flow Integration**
- [ ] Extract FSR 3 optical flow to standalone module
- [ ] Create Vulkan compute pipeline
- [ ] Validate motion vector output
- [ ] Benchmark performance

**Milestone 1.3: Frame Generation**
- [ ] Port FSR 3 frame interpolation shaders
- [ ] Implement basic presentation
- [ ] Test with captured game footage
- [ ] Profile and optimize

**Deliverable:** Single-GPU frame generation on captured content

---

### Phase 2: Dual-GPU Implementation
**Duration:** 4-6 weeks

**Milestone 2.1: Multi-GPU Foundation**
- [ ] Implement Vulkan device group management
- [ ] Create inter-GPU transfer pipeline
- [ ] Benchmark transfer bandwidth
- [ ] Optimize for target configurations

**Milestone 2.2: Pipeline Integration**
- [ ] Connect capture → transfer → generation → present
- [ ] Implement frame pacing logic
- [ ] Add configuration options
- [ ] Create user interface

**Milestone 2.3: Testing & Polish**
- [ ] Test across GPU combinations
- [ ] Handle edge cases (resolution changes, alt-tab)
- [ ] Add error recovery
- [ ] Performance optimization pass

**Deliverable:** Functional dual-GPU frame generation

---

### Phase 3: Production Readiness
**Duration:** 4-8 weeks

- [ ] Linux port via native Vulkan
- [ ] VRR/Adaptive sync support
- [ ] HDR passthrough
- [ ] Comprehensive documentation
- [ ] Community feedback integration
- [ ] Performance profiling tools

**Deliverable:** Public release candidate

---

## Research Questions

### High Priority

1. **Can FSR 3 optical flow work standalone without engine integration?**
   - FSR 3.1+ supports this via the "prepare" pass
   - Need to validate quality without motion vector hints

2. **What is the minimum secondary GPU spec for usable frame generation?**
   - lsfg-vk reports GTX 1050 Ti as minimum
   - Need to test integrated GPUs (Intel Iris, AMD 780M)

3. **How to handle UI without engine access?**
   - Frame differencing may detect static regions
   - Could train lightweight segmentation model

4. **What's the latency budget breakdown?**
   - Target: < 1 frame of additional latency
   - Capture: ~2ms, Transfer: ~3ms, Generation: ~4ms, Pacing: ~2ms

### Medium Priority

5. **Can we reduce bandwidth via compression?**
   - BC7 compression would reduce 4x
   - GPU encode/decode adds latency

6. **Is Vulkan peer-to-peer reliable across vendors?**
   - May need fallback to host staging

7. **How to handle variable refresh rate games?**
   - Adaptive multiplier based on frame time variance

### Low Priority (Future)

8. **Could a custom ML model improve quality?**
   - RIFE, FILM architectures show promise
   - Would require significant training data

9. **Is exclusive fullscreen capture feasible?**
   - Would require API hooking
   - Anti-cheat compatibility concerns

---

## Testing Methodology

### Benchmark Suite

| Test | Metric | Target |
|------|--------|--------|
| Capture latency | ms per frame | < 5ms |
| Transfer throughput | GB/s | > 2 GB/s |
| Optical flow time | ms per frame | < 3ms |
| Interpolation time | ms per frame | < 4ms |
| End-to-end latency | ms added | < 16ms (1 frame @ 60fps) |
| Visual quality | SSIM vs ground truth | > 0.95 |

### Test Content

1. **Synthetic patterns** - Known motion for validation
2. **Game footage** - Captured gameplay at various framerates
3. **Live games** - Real-time testing across genres

### Quality Evaluation

- **Objective:** SSIM, PSNR, VMAF against ground truth frames
- **Subjective:** Side-by-side comparison with Lossless Scaling
- **Artifact detection:** Motion blur, ghosting, tearing

---

## Known Challenges

### Technical

| Challenge | Mitigation Strategy |
|-----------|---------------------|
| DXGI capture drops frames under load | Optimize poll frequency, use timeout=0 |
| PCIe bandwidth limits 4K | Investigate compression, prioritize 1440p |
| UI artifacts | Implement detection/separation layer |
| VRR compatibility | Careful pacing implementation |
| Cross-vendor GPU compatibility | Extensive testing matrix, fallbacks |

### Practical

| Challenge | Mitigation Strategy |
|-----------|---------------------|
| FSR 3 shader complexity | Incremental porting, thorough documentation |
| Driver quirks | Community testing, issue tracking |
| Anti-cheat interference | Focus on desktop duplication (non-invasive) |

---

## References & Resources

### Documentation

- [AMD FidelityFX SDK Documentation](https://gpuopen.com/manuals/fidelityfx_sdk/)
- [Vulkan Specification](https://registry.khronos.org/vulkan/specs/1.3/html/)
- [DXGI Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [Windows Graphics Capture](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture)

### Source Code

- FSR 3 SDK: `github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`
- lsfg-vk: `github.com/PancakeTAS/lsfg-vk`
- DXVK: `github.com/doitsujin/dxvk`

### Community Resources

- Lossless Scaling Discord (for reference)
- GPUOpen Developer Forums
- Vulkan Discord

### Papers & Articles

- "Real-Time Video Frame Interpolation" - various IEEE papers
- AMD Fluid Motion Frames technical overview
- Optical flow estimation survey papers

---

## Appendix A: Development Environment Setup

### Required Tools

```bash
# Windows
- Visual Studio 2022 with C++ workload
- Vulkan SDK 1.3+
- Windows SDK 10.0.22000+
- CMake 3.20+
- vcpkg package manager

# Linux (future)
- GCC 12+ or Clang 15+
- Vulkan SDK
- X11/Wayland dev headers
- Meson build system
```

### FSR 3 Build Instructions

```bash
git clone https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
cd FidelityFX-SDK
# Follow SDK build instructions for your platform
```

### Project Structure (Proposed)

```
osfg/
├── src/
│   ├── capture/           # Frame capture backends
│   ├── transfer/          # Inter-GPU transfer
│   ├── opticalflow/       # Motion estimation
│   ├── interpolation/     # Frame generation
│   ├── presentation/      # Display output
│   └── app/               # Application layer
├── shaders/               # Vulkan GLSL/SPIR-V
├── external/              # Third-party dependencies
├── tests/                 # Test suite
└── docs/                  # Documentation
```

---

## Appendix B: GPU Capability Matrix

### Tested Configurations

| Primary GPU | Secondary GPU | PCIe Config | Status |
|-------------|---------------|-------------|--------|
| RTX 3080 Ti | GTX 1080 | 3.0 x8 + x8 | Reference |
| RTX 4060 | RX 570 | 4.0 x8 + 3.0 x4 | Community |
| RX 7800 XT | Radeon 780M | Laptop | Untested |

### Secondary GPU Capability Targets

| Resolution | Min GPU (x2) | Recommended (x4) |
|------------|--------------|------------------|
| 1080p | GTX 1050 Ti | GTX 1060 |
| 1440p | GTX 1060 | GTX 1070 |
| 4K | GTX 1070 | GTX 1080 |

---

## Document History

| Version | Date | Changes |
|---------|------|---------|
| 0.1 | Dec 2025 | Initial research compilation |

---

*This document will be updated as research progresses and implementation details are refined.*
