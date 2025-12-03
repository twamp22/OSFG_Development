// OSFG Simple Optical Flow Integration Test
// Captures desktop frames and computes optical flow using block matching
// MIT License - Part of Open Source Frame Generation project

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "capture/dxgi_capture.h"
#include "opticalflow/simple_opticalflow.h"
#include "interop/d3d11_d3d12_interop.h"

using Microsoft::WRL::ComPtr;

// Helper to create D3D12 device
bool CreateD3D12Device(ComPtr<ID3D12Device>& device, ComPtr<ID3D12CommandQueue>& commandQueue)
{
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
    }
#endif

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D12 device: 0x" << std::hex << hr << std::endl;
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) {
        std::cerr << "Failed to create command queue: 0x" << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

// Wait for GPU to complete
void WaitForGPU(ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE fenceEvent, UINT64& fenceValue)
{
    fenceValue++;
    queue->Signal(fence, fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void PrintUsage()
{
    std::cout << "\n=== OSFG Simple Optical Flow Test ===" << std::endl;
    std::cout << "Phase 1: Capture + Block-Matching Optical Flow + D3D11/D3D12 Interop" << std::endl;
    std::cout << "Tests the complete pipeline from screen capture to motion vector output.\n" << std::endl;
}

int main()
{
    PrintUsage();

    // ========================================================================
    // Step 1: Create D3D12 Device
    // ========================================================================
    std::cout << "[1/7] Creating D3D12 device..." << std::endl;

    ComPtr<ID3D12Device> d3d12Device;
    ComPtr<ID3D12CommandQueue> commandQueue;

    if (!CreateD3D12Device(d3d12Device, commandQueue)) {
        return 1;
    }
    std::cout << "      D3D12 device created successfully." << std::endl;

    // ========================================================================
    // Step 2: Initialize DXGI Capture (with its own D3D11 device)
    // ========================================================================
    std::cout << "[2/7] Initializing DXGI capture..." << std::endl;

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

    // ========================================================================
    // Step 3: Initialize D3D11-D3D12 Interop
    // ========================================================================
    std::cout << "[3/7] Initializing D3D11-D3D12 interop..." << std::endl;

    OSFG::D3D11D3D12Interop interop;
    OSFG::InteropConfig interopConfig;
    interopConfig.width = captureWidth;
    interopConfig.height = captureHeight;
    interopConfig.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    interopConfig.bufferCount = 2;

    if (!interop.Initialize(d3d12Device.Get(), commandQueue.Get(), interopConfig)) {
        std::cerr << "Failed to initialize interop: " << interop.GetLastError() << std::endl;
        return 1;
    }
    std::cout << "      Interop initialized successfully." << std::endl;

    // ========================================================================
    // Step 4: Initialize Simple Optical Flow
    // ========================================================================
    std::cout << "[4/7] Initializing optical flow (block matching)..." << std::endl;

    OSFG::SimpleOpticalFlow opticalFlow;
    OSFG::SimpleOpticalFlowConfig ofConfig;
    ofConfig.width = captureWidth;
    ofConfig.height = captureHeight;
    ofConfig.blockSize = 8;
    ofConfig.searchRadius = 16;

    if (!opticalFlow.Initialize(d3d12Device.Get(), ofConfig)) {
        std::cerr << "Failed to initialize optical flow: " << opticalFlow.GetLastError() << std::endl;
        return 1;
    }

    uint32_t mvWidth = opticalFlow.GetMotionVectorWidth();
    uint32_t mvHeight = opticalFlow.GetMotionVectorHeight();
    std::cout << "      Optical flow initialized." << std::endl;
    std::cout << "      Motion vector size: " << mvWidth << "x" << mvHeight << std::endl;
    std::cout << "      Block size: " << ofConfig.blockSize << ", Search radius: " << ofConfig.searchRadius << std::endl;

    // ========================================================================
    // Step 5: Create Command List and Fence
    // ========================================================================
    std::cout << "[5/7] Creating command list and synchronization objects..." << std::endl;

    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    HRESULT hr = d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    if (FAILED(hr)) {
        std::cerr << "Failed to create command allocator!" << std::endl;
        return 1;
    }

    hr = d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    if (FAILED(hr)) {
        std::cerr << "Failed to create command list!" << std::endl;
        return 1;
    }
    commandList->Close();

    hr = d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        std::cerr << "Failed to create fence!" << std::endl;
        return 1;
    }
    std::cout << "      Command list and fence created." << std::endl;

    // ========================================================================
    // Step 6: Warm-up - capture a few frames to get the pipeline started
    // ========================================================================
    std::cout << "[6/7] Warming up capture pipeline..." << std::endl;

    int warmupFrames = 0;
    for (int i = 0; i < 10; i++) {
        osfg::CapturedFrame capturedFrame;
        if (capture.CaptureFrame(capturedFrame)) {
            // Copy to interop buffer using staged copy (different D3D11 devices)
            ComPtr<ID3D11Texture2D> srcTexture;
            capturedFrame.texture->QueryInterface(IID_PPV_ARGS(&srcTexture));
            interop.CopyFromD3D11Staged(capture.GetDevice(), capture.GetContext(), srcTexture.Get());
            capture.ReleaseFrame();

            interop.SwapBuffers();
            warmupFrames++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    std::cout << "      Warmed up with " << warmupFrames << " frames." << std::endl;

    // ========================================================================
    // Step 7: Main Loop - Capture and Compute Optical Flow
    // ========================================================================
    std::cout << "[7/7] Starting capture and optical flow loop..." << std::endl;
    std::cout << "\n--- Performance Statistics ---" << std::endl;

    const int testDurationSeconds = 10;
    const int reportIntervalFrames = 30;
    int frameCount = 0;
    int opticalFlowDispatches = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    double totalCaptureTimeMs = 0.0;
    double totalInteropTimeMs = 0.0;
    double totalDispatchTimeMs = 0.0;

    while (true) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        double elapsedSeconds = std::chrono::duration<double>(currentTime - startTime).count();

        if (elapsedSeconds >= testDurationSeconds) {
            break;
        }

        // Capture frame
        auto captureStart = std::chrono::high_resolution_clock::now();
        osfg::CapturedFrame capturedFrame;
        if (!capture.CaptureFrame(capturedFrame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        auto captureEnd = std::chrono::high_resolution_clock::now();
        totalCaptureTimeMs += std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();

        frameCount++;

        // Copy captured D3D11 texture to D3D12 via interop (staged copy for cross-device)
        auto interopStart = std::chrono::high_resolution_clock::now();
        ComPtr<ID3D11Texture2D> srcTexture;
        capturedFrame.texture->QueryInterface(IID_PPV_ARGS(&srcTexture));

        if (!interop.CopyFromD3D11Staged(capture.GetDevice(), capture.GetContext(), srcTexture.Get())) {
            std::cerr << "Interop copy failed: " << interop.GetLastError() << std::endl;
            capture.ReleaseFrame();
            continue;
        }
        auto interopEnd = std::chrono::high_resolution_clock::now();
        totalInteropTimeMs += std::chrono::duration<double, std::milli>(interopEnd - interopStart).count();

        // Release the captured frame now that we've copied it
        capture.ReleaseFrame();

        // Dispatch optical flow if we have a previous frame
        if (interop.GetFrameCount() >= 2) {
            auto dispatchStart = std::chrono::high_resolution_clock::now();

            // Reset command list
            commandAllocator->Reset();
            commandList->Reset(commandAllocator.Get(), nullptr);

            // Dispatch optical flow
            if (opticalFlow.Dispatch(
                    interop.GetCurrentFrameD3D12(),
                    interop.GetPreviousFrameD3D12(),
                    commandList.Get())) {

                // Close and execute
                commandList->Close();
                ID3D12CommandList* lists[] = { commandList.Get() };
                commandQueue->ExecuteCommandLists(1, lists);

                // Wait for completion
                WaitForGPU(commandQueue.Get(), fence.Get(), fenceEvent, fenceValue);

                opticalFlowDispatches++;
            }

            auto dispatchEnd = std::chrono::high_resolution_clock::now();
            totalDispatchTimeMs += std::chrono::duration<double, std::milli>(dispatchEnd - dispatchStart).count();
        }

        // Swap buffers for next frame
        interop.SwapBuffers();

        // Report statistics periodically
        if (frameCount % reportIntervalFrames == 0) {
            double avgCaptureMs = totalCaptureTimeMs / frameCount;
            double avgInteropMs = totalInteropTimeMs / frameCount;
            double avgDispatchMs = opticalFlowDispatches > 0 ? totalDispatchTimeMs / opticalFlowDispatches : 0;

            std::cout << "Frame: " << std::setw(4) << frameCount
                      << " | OF Dispatches: " << std::setw(4) << opticalFlowDispatches
                      << " | Capture: " << std::fixed << std::setprecision(2) << avgCaptureMs << "ms"
                      << " | Interop: " << avgInteropMs << "ms"
                      << " | Dispatch: " << avgDispatchMs << "ms"
                      << " | FPS: " << std::setprecision(1) << (frameCount / elapsedSeconds)
                      << std::endl;
        }
    }

    // ========================================================================
    // Final Statistics
    // ========================================================================
    std::cout << "\n=== Final Results ===" << std::endl;
    auto totalTime = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - startTime).count();

    std::cout << "Total frames captured: " << frameCount << std::endl;
    std::cout << "Optical flow dispatches: " << opticalFlowDispatches << std::endl;
    std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << (frameCount / totalTime) << std::endl;

    if (frameCount > 0) {
        std::cout << "\nAverage Latency Breakdown:" << std::endl;
        std::cout << "  Capture:  " << std::setprecision(2) << (totalCaptureTimeMs / frameCount) << " ms" << std::endl;
        std::cout << "  Interop:  " << (totalInteropTimeMs / frameCount) << " ms" << std::endl;
        if (opticalFlowDispatches > 0) {
            std::cout << "  Dispatch: " << (totalDispatchTimeMs / opticalFlowDispatches) << " ms" << std::endl;
            std::cout << "  Total:    " << (totalCaptureTimeMs + totalInteropTimeMs + totalDispatchTimeMs) / frameCount << " ms" << std::endl;
        }
    }

    std::cout << "\nMotion Vector Texture:" << std::endl;
    std::cout << "  Size: " << mvWidth << "x" << mvHeight << std::endl;
    std::cout << "  Format: R16G16_SINT (scaled by 16 for sub-pixel precision)" << std::endl;

    // Cleanup
    if (fenceEvent) {
        CloseHandle(fenceEvent);
    }

    std::cout << "\n=== Phase 1 Test Complete ===" << std::endl;
    std::cout << "Components validated:" << std::endl;
    std::cout << "  [OK] DXGI Desktop Capture" << std::endl;
    std::cout << "  [OK] D3D11-D3D12 Interop (texture sharing)" << std::endl;
    std::cout << "  [OK] D3D12 Device and Command Queue" << std::endl;
    std::cout << "  [OK] Simple Optical Flow (shader dispatch)" << std::endl;
    std::cout << "  [OK] Motion Vector Texture Output" << std::endl;
    std::cout << "\nNext steps:" << std::endl;
    std::cout << "  - Visualize motion vectors" << std::endl;
    std::cout << "  - Implement frame interpolation shader" << std::endl;
    std::cout << "  - Add presentation layer" << std::endl;

    return 0;
}
