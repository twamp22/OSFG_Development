# Testing OSFG

This guide covers testing the OSFG components and verifying proper functionality.

## Test Applications

OSFG includes several test applications in `build/bin/Release/`:

| Executable | Purpose |
|------------|---------|
| `test_dxgi_capture.exe` | Test DXGI Desktop Duplication capture |
| `test_simple_opticalflow.exe` | Test block-matching optical flow |
| `test_fsr_opticalflow.exe` | Check FSR 3 integration status |
| `test_frame_generation.exe` | Test full single-GPU pipeline |
| `test_dual_gpu_pipeline.exe` | Test dual-GPU pipeline |
| `osfg_demo.exe` | Visual demonstration application |

## Running Tests

### Build All Tests

```bash
cmake --build build --config Release
```

### DXGI Capture Test

Tests the Desktop Duplication API wrapper.

```bash
build\bin\Release\test_dxgi_capture.exe
```

**Expected Output**:
- Initializes DXGI capture
- Acquires frames from the primary display
- Reports capture timing and frame count
- Saves test frames to disk

**Common Issues**:
- Requires Windows 10/11
- Won't work over Remote Desktop
- Needs D3D11 capable GPU

### Simple Optical Flow Test

Tests the block-matching motion estimation.

```bash
build\bin\Release\test_simple_opticalflow.exe
```

**Expected Output**:
- Creates D3D12 device
- Initializes optical flow compute shaders
- Processes test frames
- Reports motion vector generation timing

**Common Issues**:
- Requires D3D12 capable GPU
- Check GPU memory availability

### FSR Optical Flow Test

Checks FidelityFX SDK integration status.

```bash
build\bin\Release\test_fsr_opticalflow.exe
```

**Expected Output**:
```
=== OSFG FSR 3 Optical Flow Status ===

Checking for FidelityFX DLL...
  FidelityFX DLL FOUND: <path>\amd_fidelityfx_framegeneration_dx12.dll

Checking FSR optical flow availability...
  FSR optical flow is NOT available for use.
  Status: DLL present but integration pending
```

**Notes**:
- DLL detection confirms FidelityFX SDK is accessible
- Full integration requires additional work
- Falls back to SimpleOpticalFlow

### Frame Generation Test

Tests the complete single-GPU frame generation pipeline.

```bash
build\bin\Release\test_frame_generation.exe
```

**Expected Output**:
- Captures frames from display
- Computes optical flow
- Generates interpolated frames
- Reports pipeline timing

### Dual-GPU Pipeline Test

Tests the multi-GPU frame generation pipeline.

```bash
build\bin\Release\test_dual_gpu_pipeline.exe
```

**Requirements**:
- Two GPUs (primary + secondary)
- Both GPUs must support D3D12

**Expected Output**:
- Enumerates available GPUs
- Sets up cross-GPU transfer
- Runs frame generation pipeline
- Reports statistics for each stage

### OSFG Demo

Visual demonstration with real-time display.

```bash
build\bin\Release\osfg_demo.exe
```

**Controls**:
- `Alt+F10` - Toggle frame generation
- `Alt+F12` - Cycle multiplier (2X/3X/4X)
- `Escape` - Exit

## Performance Testing

### Timing Metrics

Each test application reports timing for key operations:

| Metric | Target | Location |
|--------|--------|----------|
| Capture Latency | < 5ms | DXGI acquire |
| Optical Flow | < 4ms | Compute dispatch |
| Interpolation | < 2ms | Compute dispatch |
| Transfer | < 2ms | GPU-to-GPU copy |
| Total | < 16ms | End-to-end |

### GPU Memory Usage

Monitor VRAM usage with:
- Task Manager (Performance tab)
- GPU-Z
- `dxdiag` command

Target: < 500MB per GPU

### CPU Overhead

Monitor CPU usage with:
- Task Manager
- Performance Monitor

Target: < 5% single core

## Automated Testing

### Running All Tests

```bash
# Build and run all tests
cmake --build build --config Release
cd build\bin\Release
test_dxgi_capture.exe
test_simple_opticalflow.exe
test_fsr_opticalflow.exe
test_frame_generation.exe
```

### CI/CD Integration

For automated builds:

```yaml
# Example GitHub Actions workflow
steps:
  - uses: actions/checkout@v3
  - name: Configure
    run: cmake -B build -G "Visual Studio 17 2022" -A x64
  - name: Build
    run: cmake --build build --config Release
  - name: Test FSR Status
    run: build\bin\Release\test_fsr_opticalflow.exe
```

## Troubleshooting Tests

### Common Errors

**"Failed to create D3D12 device"**
- Check GPU driver version
- Verify D3D12 support
- Try with different GPU adapter

**"Desktop Duplication not supported"**
- Cannot run over Remote Desktop
- Requires Windows 10/11
- Check display settings

**"Failed to acquire frame"**
- Display may be in exclusive fullscreen
- Try with windowed applications
- Check DWM status

**"FidelityFX DLL not found"**
- Copy DLLs from `external/FidelityFX-SDK/Kits/FidelityFX/signedbin/`
- Or build FidelityFX SDK first

### Debug Output

Enable debug output by:
1. Running in Visual Studio debugger
2. Enabling D3D12 debug layer (development builds)
3. Checking Windows Event Viewer

## Adding New Tests

1. Create test file in `tests/` directory
2. Add executable to `CMakeLists.txt`:

```cmake
add_executable(test_new_feature
    tests/test_new_feature.cpp
)

target_link_libraries(test_new_feature PRIVATE
    osfg_module_name
)
```

3. Build and run:

```bash
cmake --build build --config Release --target test_new_feature
build\bin\Release\test_new_feature.exe
```

## See Also

- [Getting Started](getting-started.md) - Build setup
- [Debugging](debugging.md) - Debug techniques
- [Architecture](architecture.md) - System design
