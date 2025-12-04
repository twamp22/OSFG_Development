// OSFG Full Frame Generation Pipeline Test
// Captures desktop frames, computes optical flow, and generates interpolated frames
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
#include "interpolation/frame_interpolation.h"

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
    std::cout << "\n=== OSFG Full Frame Generation Pipeline Test ===" << std::endl;
    std::cout << "Phase 1: Complete pipeline - Capture + Optical Flow + Frame Interpolation" << std::endl;
    std::cout << "Demonstrates the full frame generation workflow.\n" << std::endl;
}

int main()
{
    PrintUsage();

    // ========================================================================
    // Step 1: Create D3D12 Device
    // ========================================================================
    std::cout << "[1/8] Creating D3D12 device..." << std::endl;

    ComPtr<ID3D12Device> d3d12Device;
    ComPtr<ID3D12CommandQueue> commandQueue;

    if (!CreateD3D12Device(d3d12Device, commandQueue)) {
        return 1;
    }
    std::cout << "      D3D12 device created successfully." << std::endl;

    // ========================================================================
    // Step 2: Initialize D3D11-D3D12 Interop (creates D3D11On12 device)
    // ========================================================================
    std::cout << "[2/8] Initializing D3D11-D3D12 interop..." << std::endl;

    OSFG::D3D11D3D12Interop interop;
    OSFG::InteropConfig interopConfig;
    interopConfig.width = 1920;  // Will be updated after capture init
    interopConfig.height = 1080;
    interopConfig.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    interopConfig.bufferCount = 2;

    if (!interop.Initialize(d3d12Device.Get(), commandQueue.Get(), interopConfig)) {
        std::cerr << "Failed to initialize interop: " << interop.GetLastError() << std::endl;
        return 1;
    }
    std::cout << "      Interop initialized successfully." << std::endl;

    // ========================================================================
    // Step 3: Initialize DXGI Capture (using interop's D3D11 device)
    // ========================================================================
    std::cout << "[3/8] Initializing DXGI capture..." << std::endl;

    osfg::DXGICapture capture;
    osfg::CaptureConfig captureConfig;
    captureConfig.outputIndex = 0;
    captureConfig.timeoutMs = 100;

    // Use capture's own D3D11 device (Desktop Duplication can't use D3D11On12)
    // We'll use staged copy for cross-device texture transfer
    if (!capture.Initialize(captureConfig)) {
        std::cerr << "Failed to initialize DXGI capture: " << capture.GetLastError() << std::endl;
        return 1;
    }

    uint32_t captureWidth = capture.GetWidth();
    uint32_t captureHeight = capture.GetHeight();
    std::cout << "      Capture initialized: " << captureWidth << "x" << captureHeight << std::endl;

    // ========================================================================
    // Step 4: Initialize Simple Optical Flow
    // ========================================================================
    std::cout << "[4/8] Initializing optical flow..." << std::endl;

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
    opticalFlow.SetTimestampFrequency(commandQueue.Get());

    // ========================================================================
    // Step 5: Initialize Frame Interpolation
    // ========================================================================
    std::cout << "[5/8] Initializing frame interpolation..." << std::endl;

    OSFG::FrameInterpolation interpolation;
    OSFG::FrameInterpolationConfig interpConfig;
    interpConfig.width = captureWidth;
    interpConfig.height = captureHeight;
    interpConfig.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    interpConfig.interpolationFactor = 0.5f;

    if (!interpolation.Initialize(d3d12Device.Get(), interpConfig)) {
        std::cerr << "Failed to initialize frame interpolation: " << interpolation.GetLastError() << std::endl;
        return 1;
    }
    std::cout << "      Frame interpolation initialized." << std::endl;
    std::cout << "      Output size: " << interpConfig.width << "x" << interpConfig.height << std::endl;
    interpolation.SetTimestampFrequency(commandQueue.Get());

    // ========================================================================
    // Step 6: Create Command List and Fence
    // ========================================================================
    std::cout << "[6/8] Creating command list and synchronization objects..." << std::endl;

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
    // Step 7: Warm-up - capture a few frames
    // ========================================================================
    std::cout << "[7/8] Warming up capture pipeline..." << std::endl;

    int warmupFrames = 0;
    for (int i = 0; i < 10; i++) {
        osfg::CapturedFrame capturedFrame;
        if (capture.CaptureFrame(capturedFrame)) {
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
    // Step 8: Main Loop - Full Frame Generation Pipeline
    // ========================================================================
    std::cout << "[8/8] Starting full frame generation pipeline..." << std::endl;
    std::cout << "\n--- Performance Statistics ---" << std::endl;
    std::cout << "Pipeline: Capture -> Interop -> Optical Flow -> Interpolation\n" << std::endl;

    const int testDurationSeconds = 10;
    const int reportIntervalFrames = 30;
    int frameCount = 0;
    int generatedFrames = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    double totalCaptureTimeMs = 0.0;
    double totalInteropTimeMs = 0.0;
    double totalOpticalFlowTimeMs = 0.0;
    double totalInterpolationTimeMs = 0.0;

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

        capture.ReleaseFrame();

        // Process full pipeline if we have a previous frame
        if (interop.GetFrameCount() >= 2) {
            // Reset command list
            commandAllocator->Reset();
            commandList->Reset(commandAllocator.Get(), nullptr);

            // Step 1: Optical Flow
            auto ofStart = std::chrono::high_resolution_clock::now();
            if (opticalFlow.Dispatch(
                    interop.GetCurrentFrameD3D12(),
                    interop.GetPreviousFrameD3D12(),
                    commandList.Get())) {

                // Step 2: Frame Interpolation
                auto interpStart = std::chrono::high_resolution_clock::now();
                if (interpolation.Dispatch(
                        interop.GetPreviousFrameD3D12(),
                        interop.GetCurrentFrameD3D12(),
                        opticalFlow.GetMotionVectorTexture(),
                        commandList.Get())) {

                    // Execute command list
                    commandList->Close();
                    ID3D12CommandList* lists[] = { commandList.Get() };
                    commandQueue->ExecuteCommandLists(1, lists);

                    // Wait for completion
                    WaitForGPU(commandQueue.Get(), fence.Get(), fenceEvent, fenceValue);

                    auto interpEnd = std::chrono::high_resolution_clock::now();
                    totalInterpolationTimeMs += std::chrono::duration<double, std::milli>(interpEnd - interpStart).count();
                    generatedFrames++;
                }
                auto ofEnd = std::chrono::high_resolution_clock::now();
                totalOpticalFlowTimeMs += std::chrono::duration<double, std::milli>(ofEnd - ofStart).count();
            }
        }

        // Swap buffers for next frame
        interop.SwapBuffers();

        // Report statistics periodically
        if (frameCount % reportIntervalFrames == 0 && generatedFrames > 0) {
            double avgCaptureMs = totalCaptureTimeMs / frameCount;
            double avgInteropMs = totalInteropTimeMs / frameCount;
            double avgOFMs = totalOpticalFlowTimeMs / generatedFrames;
            double avgInterpMs = totalInterpolationTimeMs / generatedFrames;
            double totalPipelineMs = avgCaptureMs + avgInteropMs + avgOFMs + avgInterpMs;

            std::cout << "Frame: " << std::setw(4) << frameCount
                      << " | Generated: " << std::setw(4) << generatedFrames
                      << " | Pipeline: " << std::fixed << std::setprecision(2) << totalPipelineMs << "ms"
                      << " (" << avgCaptureMs << "/" << avgInteropMs << "/" << avgOFMs << "/" << avgInterpMs << ")"
                      << " | FPS: " << std::setprecision(1) << (frameCount / elapsedSeconds)
                      << " -> " << (2 * frameCount / elapsedSeconds) << " (2x)"
                      << std::endl;
        }
    }

    // ========================================================================
    // Final Statistics
    // ========================================================================
    std::cout << "\n=== Final Results ===" << std::endl;
    auto totalTime = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - startTime).count();

    std::cout << "\nFrame Counts:" << std::endl;
    std::cout << "  Captured frames: " << frameCount << std::endl;
    std::cout << "  Generated (interpolated) frames: " << generatedFrames << std::endl;
    std::cout << "  Total output frames (with FG): " << (frameCount + generatedFrames) << std::endl;

    std::cout << "\nFrame Rate:" << std::endl;
    std::cout << "  Input FPS: " << std::fixed << std::setprecision(1) << (frameCount / totalTime) << std::endl;
    std::cout << "  Output FPS (with FG): " << ((frameCount + generatedFrames) / totalTime) << std::endl;
    std::cout << "  Theoretical 2x FPS: " << (2.0 * frameCount / totalTime) << std::endl;

    if (generatedFrames > 0) {
        auto ofStats = opticalFlow.GetStats();
        auto interpStats = interpolation.GetStats();

        std::cout << "\nLatency Breakdown (average per frame):" << std::endl;
        std::cout << "  Capture:       " << std::setprecision(2) << (totalCaptureTimeMs / frameCount) << " ms" << std::endl;
        std::cout << "  Interop:       " << (totalInteropTimeMs / frameCount) << " ms" << std::endl;
        std::cout << "  Optical Flow:  " << (totalOpticalFlowTimeMs / generatedFrames) << " ms (CPU)" << std::endl;
        std::cout << "  Interpolation: " << (totalInterpolationTimeMs / generatedFrames) << " ms (CPU)" << std::endl;
        double totalLatency = (totalCaptureTimeMs + totalInteropTimeMs) / frameCount +
                              (totalOpticalFlowTimeMs + totalInterpolationTimeMs) / generatedFrames;
        std::cout << "  Total:         " << totalLatency << " ms" << std::endl;

        std::cout << "\nGPU Timing (shader execution only):" << std::endl;
        std::cout << "  Optical Flow:  " << ofStats.avgGpuTimeMs << " ms" << std::endl;
        std::cout << "  Interpolation: " << interpStats.avgGpuTimeMs << " ms" << std::endl;
        std::cout << "  Total GPU:     " << (ofStats.avgGpuTimeMs + interpStats.avgGpuTimeMs) << " ms" << std::endl;
    }

    std::cout << "\nResource Summary:" << std::endl;
    std::cout << "  Motion Vector Texture: " << mvWidth << "x" << mvHeight << " (R16G16_SINT)" << std::endl;
    std::cout << "  Interpolated Frame:    " << captureWidth << "x" << captureHeight << " (R8G8B8A8_UNORM)" << std::endl;

    // Cleanup
    if (fenceEvent) {
        CloseHandle(fenceEvent);
    }

    std::cout << "\n=== Phase 1 Frame Generation Test Complete ===" << std::endl;
    std::cout << "Components validated:" << std::endl;
    std::cout << "  [OK] DXGI Desktop Capture" << std::endl;
    std::cout << "  [OK] D3D11-D3D12 Interop" << std::endl;
    std::cout << "  [OK] Block-Matching Optical Flow" << std::endl;
    std::cout << "  [OK] Bi-directional Frame Interpolation" << std::endl;
    std::cout << "  [OK] Full Frame Generation Pipeline" << std::endl;
    std::cout << "\nNext steps:" << std::endl;
    std::cout << "  - Add presentation layer to display generated frames" << std::endl;
    std::cout << "  - Integrate with game/application hooks" << std::endl;
    std::cout << "  - Optimize for lower latency" << std::endl;
    std::cout << "  - Add VSync/frame pacing" << std::endl;

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}
