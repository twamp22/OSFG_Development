# Getting Started with OSFG

This guide covers building OSFG from source and running your first frame generation session.

## Prerequisites

### Required Software

| Software | Version | Download |
|----------|---------|----------|
| Visual Studio 2022 | 17.0+ | [Download](https://visualstudio.microsoft.com/) |
| CMake | 3.20+ | [Download](https://cmake.org/download/) |
| Windows SDK | 10.0+ | Included with VS |
| vcpkg | Latest | [GitHub](https://github.com/microsoft/vcpkg) |

### Visual Studio Workloads

Install the following workloads in Visual Studio Installer:
- Desktop development with C++
- Game development with C++ (optional, for DirectX tools)

### vcpkg Setup

```bash
# Clone vcpkg (if not already installed)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat

# Install required packages
vcpkg install directx-headers:x64-windows
```

## Building OSFG

### 1. Clone the Repository

```bash
git clone https://github.com/GydroGames/OSFG.git
cd OSFG
```

### 2. Initialize Submodules

The FidelityFX SDK is included as an external dependency:

```bash
git submodule update --init --recursive
```

### 3. Configure with CMake

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

### 4. Build

```bash
# Build all targets
cmake --build build --config Release

# Or build specific targets
cmake --build build --config Release --target osfg_demo
```

### 5. Verify Build

After building, you should find the following in `build/bin/Release/`:

| Executable | Description |
|------------|-------------|
| `test_dxgi_capture.exe` | Tests DXGI capture functionality |
| `test_simple_opticalflow.exe` | Tests optical flow computation |
| `test_frame_generation.exe` | Tests full pipeline |
| `osfg_demo.exe` | Visual demonstration application |

## Quick Start

### Running the Demo

1. Start any windowed application (game, video, etc.)
2. Run `osfg_demo.exe`
3. The demo will capture your screen and display frame-generated output

### Basic Controls

| Hotkey | Action |
|--------|--------|
| Alt+F10 | Toggle frame generation on/off |
| Alt+F11 | Toggle statistics overlay |
| Alt+F12 | Cycle frame generation mode (2X/3X/4X) |
| Escape | Exit application |

### Configuration

OSFG creates a configuration file at:
```
%APPDATA%\OSFG\config.ini
```

See [Configuration Guide](configuration.md) for detailed settings.

## Troubleshooting

### Common Issues

#### "Desktop duplication not available"

Another application is using desktop duplication. Close screen recording software, streaming tools, or other capture applications.

#### "Failed to create D3D12 device"

- Update your GPU drivers
- Ensure your GPU supports DirectX 12
- Check Windows version (requires Windows 10 1903+)

#### High latency or stuttering

- Reduce capture resolution
- Disable VSync in the target application
- Use dual-GPU mode if available

#### Build errors with vcpkg

Ensure vcpkg is installed at `C:\vcpkg` or update the path in `CMakeLists.txt`:

```cmake
set(VCPKG_ROOT "C:/your/vcpkg/path")
```

### Debug Build

For debugging, build with Debug configuration:

```bash
cmake --build build --config Debug
```

Debug builds include:
- D3D12 debug layer validation
- Additional logging
- Shader debugging symbols

## Next Steps

- [Architecture Overview](architecture.md) - Understand how OSFG works
- [Configuration Guide](configuration.md) - Customize settings
- [API Reference](api/capture.md) - Integrate OSFG into your project
