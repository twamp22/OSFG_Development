# OSFG Documentation

Welcome to the Open Source Frame Generation (OSFG) documentation.

## Table of Contents

### Getting Started
- [Building OSFG](getting-started.md)
- [Quick Start Guide](getting-started.md#quick-start)
- [Configuration](configuration.md)

### Architecture
- [System Overview](architecture.md)
- [Pipeline Architecture](architecture.md#pipeline-architecture)
- [Data Flow](architecture.md#data-flow)

### API Reference
- [Capture Module](api/capture.md) - DXGI Desktop Duplication
- [Optical Flow Module](api/opticalflow.md) - Simple block-matching motion estimation
- [FSR Optical Flow Module](api/fsr-opticalflow.md) - AMD FidelityFX optical flow (stub)
- [Interpolation Module](api/interpolation.md) - Frame generation
- [Presentation Module](api/presentation.md) - Display output
- [Transfer Module](api/transfer.md) - Inter-GPU transfer
- [Pipeline Module](api/pipeline.md) - Dual-GPU pipeline orchestration
- [Application Module](api/app.md) - Config, hotkeys, overlay

### Development
- [Contributing](contributing.md)
- [Testing](testing.md)
- [Debugging](debugging.md)

## Project Overview

OSFG is an open source frame generation system that:

1. **Captures frames** from any running application using DXGI Desktop Duplication
2. **Analyzes motion** using block-matching optical flow algorithms
3. **Generates intermediate frames** through motion-compensated interpolation
4. **Presents output** with proper frame pacing for smooth playback

### Key Features

- **Universal Compatibility**: Works with any game or application without engine integration
- **Dual-GPU Support**: Offload frame generation to a secondary GPU
- **Low Latency**: Optimized pipeline targeting < 16ms total latency
- **Configurable**: INI-based configuration with runtime hotkey control
- **Performance Overlay**: Real-time statistics display

### Supported Platforms

- Windows 10/11 (64-bit)
- DirectX 11/12 capable GPUs

## Quick Links

| Resource | Description |
|----------|-------------|
| [GitHub Repository](https://github.com/GydroGames/OSFG) | Source code |
| [Research Plan](../OSFG_Development_Research_Plan.md) | Detailed R&D documentation |
| [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) | AMD's open source effects library |

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 0.2.0 | Dec 2025 | Phase 2: Dual-GPU pipeline orchestration |
| 0.1.0 | Dec 2025 | Initial Phase 1 implementation |
