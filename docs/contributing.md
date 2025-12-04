# Contributing to OSFG

Thank you for your interest in contributing to the Open Source Frame Generation project!

## Getting Started

1. Fork the repository
2. Clone your fork locally
3. Set up the development environment (see [Getting Started](getting-started.md))
4. Create a feature branch for your changes

## Development Environment

### Prerequisites

- Windows 10/11 64-bit
- Visual Studio 2022 with C++ workload
- CMake 3.20+
- Windows SDK 10.0+
- vcpkg with DirectX Headers

### Building

```bash
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release
```

## Code Style

### C++ Guidelines

- Use C++17 features where appropriate
- Follow existing code patterns in the repository
- Use `OSFG` namespace for all public types
- Prefix private members with `m_`
- Use `ComPtr` for D3D12 objects

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Classes | PascalCase | `DualGPUPipeline` |
| Functions | PascalCase | `Initialize()` |
| Variables | camelCase | `frameCount` |
| Members | m_camelCase | `m_device` |
| Constants | UPPER_SNAKE | `MAX_FRAMES` |

### File Organization

```
src/
├── module_name/
│   ├── class_name.cpp
│   └── class_name.h
```

## Pull Request Process

1. Ensure your code builds without warnings
2. Add or update tests for new functionality
3. Update documentation if needed
4. Create a pull request with a clear description

### PR Title Format

```
[Module] Brief description

Examples:
[Pipeline] Add frame pacing configuration
[Capture] Fix multi-monitor support
[Docs] Update API reference
```

### PR Description Template

```markdown
## Summary
Brief description of changes

## Changes
- List of specific changes
- One item per line

## Testing
How the changes were tested

## Related Issues
Fixes #123
```

## Areas for Contribution

### High Priority

1. **FidelityFX Integration**
   - Implement full frame generation using FFX DLLs
   - Create abstraction layer for optical flow backends

2. **Performance Optimization**
   - Profile and optimize critical paths
   - Reduce GPU memory usage

3. **Testing**
   - Add unit tests for core modules
   - Create integration tests

### Medium Priority

4. **Documentation**
   - Improve API documentation
   - Add usage examples
   - Create tutorials

5. **Linux Support**
   - Port to Vulkan
   - Implement X11/Wayland capture

### Good First Issues

- Add configuration validation
- Improve error messages
- Fix compiler warnings
- Update documentation

## Module Overview

| Module | Purpose | Complexity |
|--------|---------|------------|
| `osfg_capture` | DXGI frame capture | Medium |
| `osfg_simple_opticalflow` | Motion estimation | High |
| `osfg_fsr_opticalflow` | FSR 3 optical flow | High |
| `osfg_interpolation` | Frame generation | High |
| `osfg_transfer` | GPU-to-GPU transfer | Medium |
| `osfg_pipeline` | Pipeline orchestration | Medium |
| `osfg_app` | Config, hotkeys, overlay | Low |
| `osfg_presentation` | Display output | Medium |
| `osfg_interop` | D3D11/D3D12 sharing | Medium |

## Testing Your Changes

```bash
# Build all tests
cmake --build build --config Release

# Run specific test
build\bin\Release\test_dxgi_capture.exe
build\bin\Release\test_simple_opticalflow.exe
build\bin\Release\test_fsr_opticalflow.exe
build\bin\Release\test_frame_generation.exe
build\bin\Release\test_dual_gpu_pipeline.exe
```

## Questions?

- Open an issue for bugs or feature requests
- Check existing issues before creating new ones
- Reference the [Architecture](architecture.md) document for design questions

## License

By contributing, you agree that your contributions will be licensed under the same terms as the project (MIT License for FidelityFX components).
