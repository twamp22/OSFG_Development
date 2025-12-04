# Debugging OSFG

This guide covers debugging techniques for OSFG development and troubleshooting.

## Debug Build

### Building with Debug Symbols

```bash
# Configure with Debug
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Debug configuration
cmake --build build --config Debug
```

Debug binaries are in `build/bin/Debug/`.

### Visual Studio Debugging

1. Open the generated solution: `build/OSFG.sln`
2. Set startup project (e.g., `test_frame_generation`)
3. Set configuration to Debug
4. Press F5 to start debugging

## D3D12 Debug Layer

### Enabling Debug Layer

Add to initialization code:

```cpp
#if defined(_DEBUG)
{
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
}
#endif
```

### GPU-Based Validation

For thorough GPU validation:

```cpp
ComPtr<ID3D12Debug1> debugController1;
if (SUCCEEDED(debugController.As(&debugController1))) {
    debugController1->SetEnableGPUBasedValidation(TRUE);
}
```

**Note**: GPU-based validation significantly impacts performance.

### Debug Layer Output

Debug messages appear in:
- Visual Studio Output window
- DebugView (Sysinternals)
- Windows Event Viewer

## Common Issues

### DXGI Capture Issues

**Problem**: `AcquireNextFrame` returns `DXGI_ERROR_ACCESS_LOST`

**Cause**: Desktop mode changed, display settings changed, or GPU reset

**Solution**:
```cpp
if (hr == DXGI_ERROR_ACCESS_LOST) {
    // Recreate duplication interface
    m_duplication.Reset();
    CreateDuplicationInterface();
}
```

**Problem**: Black frames captured

**Cause**: Application using exclusive fullscreen, DWM disabled

**Solution**: Use windowed or borderless windowed mode

### D3D12 Resource Issues

**Problem**: Resource state mismatch

**Symptoms**: D3D12 debug layer errors about invalid resource state

**Solution**: Track and transition resource states properly:
```cpp
D3D12_RESOURCE_BARRIER barrier = {};
barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
barrier.Transition.pResource = resource;
barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
commandList->ResourceBarrier(1, &barrier);
```

**Problem**: GPU hang or device removed

**Symptoms**: `DXGI_ERROR_DEVICE_REMOVED` or `DXGI_ERROR_DEVICE_RESET`

**Debug**: Get removal reason:
```cpp
HRESULT reason = m_device->GetDeviceRemovedReason();
// Check reason code
```

### Cross-GPU Transfer Issues

**Problem**: Cross-adapter heap creation fails

**Cause**: GPUs may not support cross-adapter shared heaps

**Solution**: Fall back to CPU staging:
```cpp
if (!CreateCrossAdapterHeap()) {
    // Use CPU staging fallback
    CreateStagingBuffer();
}
```

### Shader Compilation Issues

**Problem**: Shader compilation fails at runtime

**Debug**: Enable shader debug output:
```cpp
UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
ComPtr<ID3DBlob> errors;
HRESULT hr = D3DCompile(shaderSource, sourceSize, nullptr, nullptr, nullptr,
                        entryPoint, target, compileFlags, 0, &shader, &errors);
if (FAILED(hr) && errors) {
    OutputDebugStringA((char*)errors->GetBufferPointer());
}
```

## Performance Debugging

### PIX for Windows

Use PIX for GPU debugging and profiling:

1. Download PIX from Microsoft
2. Launch application through PIX
3. Capture GPU frames
4. Analyze timing and resources

### GPU Profiling

Add timing queries:

```cpp
// Create query heap
D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
queryHeapDesc.Count = 2;
device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&queryHeap));

// In command list
commandList->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
// ... GPU work ...
commandList->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
commandList->ResolveQueryData(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readbackBuffer, 0);
```

### CPU Profiling

Use Visual Studio Profiler:
1. Debug > Performance Profiler
2. Select CPU Usage
3. Start profiling
4. Analyze hot paths

## Memory Debugging

### GPU Memory Leaks

Check for leaks with D3D12 debug layer:

```cpp
// At shutdown
ComPtr<IDXGIDebug1> dxgiDebug;
if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
    dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
}
```

### Object Naming

Name D3D12 objects for easier debugging:

```cpp
resource->SetName(L"MotionVectorTexture");
commandQueue->SetName(L"MainCommandQueue");
```

Named objects appear in PIX and debug output.

## Logging

### Adding Debug Output

```cpp
#ifdef _DEBUG
#define OSFG_LOG(fmt, ...) \
    { char buf[256]; sprintf_s(buf, "[OSFG] " fmt "\n", __VA_ARGS__); OutputDebugStringA(buf); }
#else
#define OSFG_LOG(fmt, ...)
#endif

// Usage
OSFG_LOG("Frame %llu captured in %.2fms", frameId, captureTime);
```

### Error Reporting

Check HRESULT values:

```cpp
inline void ThrowIfFailed(HRESULT hr, const char* context = nullptr) {
    if (FAILED(hr)) {
        char msg[256];
        sprintf_s(msg, "HRESULT 0x%08X at %s", hr, context ? context : "unknown");
        throw std::runtime_error(msg);
    }
}

// Usage
ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
```

## Debugging Specific Modules

### osfg_capture

- Enable DXGI debug layer
- Check `AcquireNextFrame` return codes
- Verify frame timing with `GetFrameStatistics`

### osfg_simple_opticalflow

- Verify compute shader compilation
- Check UAV bindings
- Validate motion vector output range

### osfg_interpolation

- Verify input textures are valid
- Check motion vector scaling
- Validate output frame quality

### osfg_transfer

- Check cross-adapter heap support
- Verify copy fence synchronization
- Monitor transfer bandwidth

### osfg_pipeline

- Check frame timing and pacing
- Verify GPU synchronization
- Monitor queue utilization

## Tools

| Tool | Purpose |
|------|---------|
| Visual Studio Debugger | CPU debugging, breakpoints |
| PIX for Windows | GPU capture and analysis |
| GPU-Z | GPU monitoring |
| RenderDoc | Graphics debugging |
| DebugView | Debug output viewer |
| Process Monitor | File/registry access |

## See Also

- [Testing](testing.md) - Test applications
- [Architecture](architecture.md) - System design
- [Contributing](contributing.md) - Development guidelines
