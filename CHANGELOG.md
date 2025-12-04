# Changelog

All notable changes to OSFG (Open Source Frame Generation) will be documented in this file.

## [0.2.0] - December 2025

### Added
- **Dual-GPU Pipeline** (`osfg_pipeline`)
  - `DualGPUPipeline` class for orchestrating multi-GPU frame generation
  - Frame pacing for smooth 2X/3X/4X output
  - Runtime configuration (toggle frame gen, change multiplier)
  - Pipeline statistics and callbacks
  - Test application `test_dual_gpu_pipeline.exe`

- **GPU Transfer Module** (`osfg_transfer`)
  - Cross-adapter heap support for direct GPU-to-GPU transfer
  - CPU staging fallback for universal compatibility
  - Triple-buffering for optimal throughput
  - Transfer statistics tracking

- **Application Layer** (`osfg_app`)
  - Configuration manager with INI file support
  - Global hotkey handler (Alt+F10 toggle, Alt+F12 cycle modes)
  - Direct2D statistics overlay

- **FSR 3 Optical Flow** (`osfg_fsr_opticalflow`)
  - FidelityFX SDK v2.0 successfully built
  - Pre-built DLLs available (`amd_fidelityfx_framegeneration_dx12.dll`)
  - DLL detection and path reporting (`IsDllPresent()`, `GetDllPath()`)
  - API wrapper ready for full SDK integration
  - Documentation for integration options

- **Documentation**
  - Complete API reference for all modules
  - Architecture documentation
  - Configuration guide
  - Getting started guide

### Changed
- Updated project version to 0.2.0
- Reorganized CMakeLists.txt for cleaner structure
- Enhanced README with FSR 3 integration status

## [0.1.0] - December 2025

### Added
- **DXGI Capture** (`osfg_capture`)
  - Desktop Duplication API wrapper
  - Low-latency frame acquisition
  - Monitor selection support

- **Simple Optical Flow** (`osfg_simple_opticalflow`)
  - Block-matching motion estimation
  - Luminance pyramid generation
  - D3D12 compute shader implementation

- **D3D11/D3D12 Interop** (`osfg_interop`)
  - Cross-API resource sharing
  - Keyed mutex synchronization
  - Shared texture management

- **Frame Interpolation** (`osfg_interpolation`)
  - Motion-compensated frame generation
  - Configurable interpolation factor
  - D3D12 compute shader implementation

- **Simple Presenter** (`osfg_presentation`)
  - D3D12 swap chain management
  - Basic presentation pipeline
  - VSync support

- **Test Applications**
  - `test_dxgi_capture.exe` - Capture testing
  - `test_simple_opticalflow.exe` - Optical flow testing
  - `test_frame_generation.exe` - Full pipeline test
  - `osfg_demo.exe` - Visual demonstration

### Technical Details
- Windows 10/11 64-bit support
- DirectX 11/12 compute pipeline
- C++17 standard
- CMake 3.20+ build system
