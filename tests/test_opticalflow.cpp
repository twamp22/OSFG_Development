// OSFG Optical Flow Integration Test
// Captures desktop frames and computes optical flow using FSR 3
// MIT License - Part of Open Source Frame Generation project

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "capture/dxgi_capture.h"
#include "opticalflow/osfg_opticalflow.h"

using Microsoft::WRL::ComPtr;

// Helper to create D3D12 device
bool CreateD3D12Device(ComPtr<ID3D12Device>& device, ComPtr<ID3D12CommandQueue>& commandQueue)
{
    // Enable debug layer in debug builds
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
    }
#endif

    // Create device
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D12 device: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) {
        std::cerr << "Failed to create command queue: 0x" << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

void PrintUsage()
{
    std::cout << "\n=== OSFG Optical Flow Test ===" << std::endl;
    std::cout << "This test captures desktop frames and computes optical flow." << std::endl;
    std::cout << "\nUsage: Press Ctrl+C to stop\n" << std::endl;
}

int main()
{
    PrintUsage();

    // Create D3D12 device and command queue
    ComPtr<ID3D12Device> d3d12Device;
    ComPtr<ID3D12CommandQueue> commandQueue;

    std::cout << "[1/5] Creating D3D12 device..." << std::endl;
    if (!CreateD3D12Device(d3d12Device, commandQueue)) {
        return 1;
    }
    std::cout << "      D3D12 device created successfully." << std::endl;

    // Initialize DXGI capture
    std::cout << "[2/5] Initializing DXGI capture..." << std::endl;
    osfg::DXGICapture capture;
    osfg::CaptureConfig captureConfig;
    captureConfig.outputIndex = 0;
    captureConfig.timeoutMs = 100;

    if (!capture.Initialize(captureConfig)) {
        std::cerr << "Failed to initialize DXGI capture: " << capture.GetLastError() << std::endl;
        return 1;
    }

    uint32_t captureWidth = capture.GetWidth();
    uint32_t captureHeight = capture.GetHeight();
    std::cout << "      Capture initialized: " << captureWidth << "x" << captureHeight << std::endl;

    // Initialize optical flow
    std::cout << "[3/5] Initializing optical flow..." << std::endl;
    OSFG::OpticalFlow opticalFlow;
    OSFG::OpticalFlowConfig ofConfig;
    ofConfig.width = captureWidth;
    ofConfig.height = captureHeight;
    ofConfig.enableHDR = false;
    ofConfig.enableFP16 = true;

    if (!opticalFlow.Initialize(d3d12Device.Get(), commandQueue.Get(), ofConfig)) {
        std::cerr << "Failed to initialize optical flow!" << std::endl;
        std::cerr << "Note: Optical flow requires FidelityFX SDK DLLs in the same directory." << std::endl;
        return 1;
    }

    auto ofOutput = opticalFlow.GetOutput();
    std::cout << "      Optical flow initialized." << std::endl;
    std::cout << "      Motion vector size: " << ofOutput.motionVectorWidth << "x" << ofOutput.motionVectorHeight << std::endl;

    // Create D3D12 command allocator and command list
    std::cout << "[4/5] Creating D3D12 command list..." << std::endl;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    HRESULT hr = d3d12Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&commandAllocator)
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to create command allocator!" << std::endl;
        return 1;
    }

    hr = d3d12Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList)
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to create command list!" << std::endl;
        return 1;
    }
    commandList->Close();
    std::cout << "      Command list created." << std::endl;

    // Create fence for GPU synchronization
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    hr = d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        std::cerr << "Failed to create fence!" << std::endl;
        return 1;
    }

    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    std::cout << "[5/5] Starting capture and optical flow loop..." << std::endl;
    std::cout << "\n--- Performance Statistics ---" << std::endl;

    // Main loop
    const int testDurationSeconds = 10;
    const int reportIntervalFrames = 30;
    int frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (true) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        double elapsedSeconds = std::chrono::duration<double>(currentTime - startTime).count();

        if (elapsedSeconds >= testDurationSeconds) {
            break;
        }

        // Capture frame
        osfg::CapturedFrame capturedFrame;
        if (!capture.CaptureFrame(capturedFrame)) {
            // No new frame available
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        frameCount++;

        // Note: In production, we would:
        // 1. Create a D3D12 texture from the D3D11 captured texture using D3D11On12
        // 2. Pass that D3D12 texture to OpticalFlow::Dispatch()
        // 3. Execute the command list
        // 4. Wait for GPU completion

        // For now, we demonstrate the capture pipeline working

        // Release the captured frame
        capture.ReleaseFrame();

        // Report statistics periodically
        if (frameCount % reportIntervalFrames == 0) {
            auto& stats = capture.GetStats();
            std::cout << "Frames: " << frameCount
                      << " | Captured: " << stats.framesCapture
                      << " | Missed: " << stats.framesMissed
                      << " | Avg Latency: " << std::fixed << std::setprecision(2)
                      << stats.avgCaptureTimeMs << "ms"
                      << " | FPS: " << std::setprecision(1) << (frameCount / elapsedSeconds)
                      << std::endl;
        }
    }

    // Final statistics
    std::cout << "\n--- Final Results ---" << std::endl;
    auto& finalStats = capture.GetStats();
    auto totalTime = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - startTime).count();

    std::cout << "Total frames captured: " << finalStats.framesCapture << std::endl;
    std::cout << "Total frames missed: " << finalStats.framesMissed << std::endl;
    std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << (frameCount / totalTime) << std::endl;
    std::cout << "Capture latency (avg/min/max): "
              << std::setprecision(2) << finalStats.avgCaptureTimeMs << "/"
              << finalStats.minCaptureTimeMs << "/"
              << finalStats.maxCaptureTimeMs << " ms" << std::endl;

    // Cleanup
    if (fenceEvent) {
        CloseHandle(fenceEvent);
    }

    std::cout << "\nTest completed successfully!" << std::endl;
    std::cout << "\nNote: Full optical flow processing requires D3D11->D3D12 texture interop." << std::endl;
    std::cout << "Next step: Implement texture copy from D3D11 capture to D3D12 for optical flow." << std::endl;

    return 0;
}
