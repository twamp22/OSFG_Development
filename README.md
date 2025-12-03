# OSFG - Open Source Frame Generation

An open source, dual-GPU frame generation system that captures frames from any running application, generates interpolated frames using optical flow analysis, and offloads processing to a secondary GPU for minimal overhead.

## Overview

OSFG provides a universal frame generation solution similar to Lossless Scaling, but fully open source. It enables:

- **Universal Compatibility**: Works with any game or application without engine integration
- **GPU Repurposing**: Use older GPUs as dedicated frame generators
- **Community-Driven**: Open source for improvements and ports
- **Educational Value**: Learn real-time graphics pipeline concepts

## Features

- DXGI Desktop Duplication capture for low-latency frame acquisition
- DirectX 11/12 interoperability for cross-API resource sharing
- Block-matching optical flow for motion estimation
- Frame interpolation with motion compensation
- Simple presentation system for output display

## Project Structure

```
OSFG_Development/
├── src/
│   ├── capture/          # DXGI frame capture
│   ├── interop/          # D3D11-D3D12 interoperability
│   ├── interpolation/    # Frame generation
│   ├── opticalflow/      # Motion estimation
│   ├── presentation/     # Display output
│   └── transfer/         # Inter-GPU transfer (future)
├── tests/                # Test applications
├── demos/                # Demo applications
├── shaders/              # GPU shaders
└── external/             # Third-party dependencies (FidelityFX-SDK)
```

## Requirements

### Build Tools
- Visual Studio 2022 with C++ workload
- CMake 3.20+
- Windows SDK 10.0+

### Dependencies
- vcpkg with DirectX Headers installed at `C:/vcpkg`
- AMD FidelityFX SDK (included in `external/`)

### Supported Platforms
- Windows 10/11 (64-bit)

## Building

### 1. Install vcpkg Dependencies

```bash
# Install DirectX Headers via vcpkg
vcpkg install directx-headers:x64-windows
```

### 2. Configure with CMake

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

### 3. Build

```bash
cmake --build build --config Release
```

### Build Outputs

Binaries are output to `build/bin/Release/`:

| Target | Description |
|--------|-------------|
| `test_dxgi_capture.exe` | DXGI capture test |
| `test_simple_opticalflow.exe` | Optical flow test |
| `test_frame_generation.exe` | Full pipeline test |
| `osfg_demo.exe` | Visual demo application |

## Libraries

| Library | Purpose |
|---------|---------|
| `osfg_capture` | DXGI Desktop Duplication wrapper |
| `osfg_simple_opticalflow` | Block-matching optical flow (D3D12 compute) |
| `osfg_interop` | D3D11/D3D12 resource sharing |
| `osfg_interpolation` | Motion-compensated frame generation |
| `osfg_presentation` | DirectX 12 presentation |

## Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Frame Capture  │────▶│  Optical Flow   │────▶│   Interpolation │
│  (DXGI/D3D11)   │     │    (D3D12)      │     │     (D3D12)     │
└─────────────────┘     └─────────────────┘     └─────────────────┘
                                                        │
                                                        ▼
                                                ┌─────────────────┐
                                                │   Presentation  │
                                                │     (D3D12)     │
                                                └─────────────────┘
```

### Data Flow

1. **Capture**: DXGI Desktop Duplication acquires frames from the desktop compositor
2. **Interop**: D3D11 textures are shared with D3D12 for compute processing
3. **Optical Flow**: Motion vectors computed via block matching on luminance pyramids
4. **Interpolation**: Intermediate frames generated using motion compensation
5. **Presentation**: Real and generated frames presented with proper pacing

## Development Status

**Current Phase**: Phase 1 - Single-GPU Proof of Concept

- [x] DXGI Desktop Duplication capture
- [x] D3D11/D3D12 interoperability
- [x] Simple block-matching optical flow
- [x] Basic frame interpolation
- [x] Simple presenter
- [ ] FSR 3 optical flow integration
- [ ] Dual-GPU support
- [ ] Linux support

## Technical Specifications

| Metric | Target |
|--------|--------|
| Capture Latency | < 5ms |
| Frame Generation | < 4ms |
| End-to-End Latency | < 16ms (1 frame @ 60fps) |
| VRAM Usage | < 500MB per GPU |
| CPU Overhead | < 5% single core |

## Related Resources

- [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) - MIT licensed optical flow and frame interpolation
- [DXGI Desktop Duplication API](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [Development Research Plan](OSFG_Development_Research_Plan.md) - Detailed technical planning document

## License

This project uses components from the AMD FidelityFX SDK, which is licensed under the MIT License. See the FidelityFX SDK license for details.

## Contributing

Contributions are welcome! Please see the research plan document for areas that need work and architectural decisions.

## Author

Thomas / GydroGames - December 2025
