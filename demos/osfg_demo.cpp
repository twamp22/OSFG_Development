// OSFG Demo Application
// Visual demonstration of the frame generation pipeline
// MIT License - Part of Open Source Frame Generation project

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <sstream>

#include "capture/dxgi_capture.h"
#include "opticalflow/simple_opticalflow.h"
#include "interop/d3d11_d3d12_interop.h"
#include "interpolation/frame_interpolation.h"
#include "presentation/simple_presenter.h"

using Microsoft::WRL::ComPtr;

// Application state
struct AppState {
    bool frameGenerationEnabled = true;
    bool showStats = true;
    float targetFPS = 60.0f;
    int framesProcessed = 0;
    int framesGenerated = 0;
    double avgPipelineMs = 0.0;
};

// Create D3D12 device
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
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    return SUCCEEDED(hr);
}

// Wait for GPU
void WaitForGPU(ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE fenceEvent, UINT64& fenceValue)
{
    fenceValue++;
    queue->Signal(fence, fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Allocate console for debug output
    AllocConsole();
    FILE* pFile;
    freopen_s(&pFile, "CONOUT$", "w", stdout);
    freopen_s(&pFile, "CONOUT$", "w", stderr);

    std::cout << "\n=== OSFG Frame Generation Demo ===" << std::endl;
    std::cout << "Phase 1: Visual demonstration of the complete pipeline" << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  ESC    - Exit" << std::endl;
    std::cout << "  SPACE  - Toggle frame generation on/off" << std::endl;
    std::cout << "  S      - Toggle statistics display" << std::endl;
    std::cout << "\n" << std::endl;

    // ========================================================================
    // Initialize D3D12
    // ========================================================================
    std::cout << "[1/7] Creating D3D12 device..." << std::endl;
    ComPtr<ID3D12Device> d3d12Device;
    ComPtr<ID3D12CommandQueue> commandQueue;

    if (!CreateD3D12Device(d3d12Device, commandQueue)) {
        MessageBoxW(nullptr, L"Failed to create D3D12 device", L"OSFG Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::cout << "      Done." << std::endl;

    // ========================================================================
    // Initialize DXGI Capture FIRST (to get actual screen dimensions)
    // ========================================================================
    std::cout << "[2/7] Initializing DXGI capture..." << std::endl;
    osfg::DXGICapture capture;
    osfg::CaptureConfig captureConfig;
    captureConfig.outputIndex = 0;
    captureConfig.timeoutMs = 100;

    if (!capture.Initialize(captureConfig)) {
        std::wstring msg = L"Failed to initialize DXGI capture.\n\nError: ";
        std::wstringstream ss;
        ss << capture.GetLastError().c_str();
        msg += ss.str();
        msg += L"\n\nTry running as Administrator.";
        MessageBoxW(nullptr, msg.c_str(), L"OSFG Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    uint32_t captureWidth = capture.GetWidth();
    uint32_t captureHeight = capture.GetHeight();
    std::cout << "      Resolution: " << captureWidth << "x" << captureHeight << std::endl;

    // ========================================================================
    // Initialize D3D11-D3D12 Interop (using actual capture dimensions)
    // ========================================================================
    std::cout << "[3/7] Initializing D3D11-D3D12 interop..." << std::endl;
    OSFG::D3D11D3D12Interop interop;
    OSFG::InteropConfig interopConfig;
    interopConfig.width = captureWidth;
    interopConfig.height = captureHeight;
    interopConfig.format = DXGI_FORMAT_B8G8R8A8_UNORM;

    if (!interop.Initialize(d3d12Device.Get(), commandQueue.Get(), interopConfig)) {
        MessageBoxW(nullptr, L"Failed to initialize D3D11-D3D12 interop", L"OSFG Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::cout << "      Done." << std::endl;

    // ========================================================================
    // Initialize Optical Flow
    // ========================================================================
    std::cout << "[4/7] Initializing optical flow..." << std::endl;
    OSFG::SimpleOpticalFlow opticalFlow;
    OSFG::SimpleOpticalFlowConfig ofConfig;
    ofConfig.width = captureWidth;
    ofConfig.height = captureHeight;
    ofConfig.blockSize = 8;
    ofConfig.searchRadius = 16;  // Three-step search makes larger radius efficient (~36 positions vs 1089)

    if (!opticalFlow.Initialize(d3d12Device.Get(), ofConfig)) {
        MessageBoxW(nullptr, L"Failed to initialize optical flow", L"OSFG Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::cout << "      Motion vectors: " << opticalFlow.GetMotionVectorWidth() << "x" << opticalFlow.GetMotionVectorHeight() << std::endl;

    // ========================================================================
    // Initialize Frame Interpolation
    // ========================================================================
    std::cout << "[5/7] Initializing frame interpolation..." << std::endl;
    OSFG::FrameInterpolation interpolation;
    OSFG::FrameInterpolationConfig interpConfig;
    interpConfig.width = captureWidth;
    interpConfig.height = captureHeight;
    interpConfig.format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Must match interop/capture format
    interpConfig.interpolationFactor = 0.5f;

    if (!interpolation.Initialize(d3d12Device.Get(), interpConfig)) {
        MessageBoxW(nullptr, L"Failed to initialize frame interpolation", L"OSFG Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::cout << "      Done." << std::endl;

    // ========================================================================
    // Initialize Presenter
    // ========================================================================
    std::cout << "[6/7] Creating presentation window..." << std::endl;
    OSFG::SimplePresenter presenter;
    OSFG::PresenterConfig presenterConfig;
    // Cap window size to avoid issues with very large screens
    presenterConfig.width = captureWidth > 1280 ? 1280 : captureWidth;
    presenterConfig.height = captureHeight > 720 ? 720 : captureHeight;
    presenterConfig.bufferCount = 2;
    presenterConfig.vsync = true;
    presenterConfig.windowTitle = L"OSFG Frame Generation Demo";

    if (!presenter.Initialize(d3d12Device.Get(), commandQueue.Get(), presenterConfig)) {
        MessageBoxW(nullptr, L"Failed to create presentation window", L"OSFG Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::cout << "      Window created." << std::endl;

    // ========================================================================
    // Create Command List and Fence
    // ========================================================================
    std::cout << "[7/7] Creating GPU resources..." << std::endl;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    commandList->Close();
    d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    std::cout << "      Done." << std::endl;

    // ========================================================================
    // Warm-up
    // ========================================================================
    std::cout << "\nWarming up capture pipeline..." << std::endl;
    for (int i = 0; i < 5; i++) {
        osfg::CapturedFrame capturedFrame;
        if (capture.CaptureFrame(capturedFrame)) {
            ComPtr<ID3D11Texture2D> srcTexture;
            capturedFrame.texture->QueryInterface(IID_PPV_ARGS(&srcTexture));
            interop.CopyFromD3D11Staged(capture.GetDevice(), capture.GetContext(), srcTexture.Get());
            capture.ReleaseFrame();
            interop.SwapBuffers();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // ========================================================================
    // Main Loop
    // ========================================================================
    std::cout << "\n=== Starting Frame Generation ===" << std::endl;
    std::cout << "Press ESC to exit, SPACE to toggle FG, S to toggle stats\n" << std::endl;

    AppState state;
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastReportTime = startTime;
    double totalPipelineMs = 0.0;
    bool showNextFrame = true;  // Alternates between captured and generated

    while (presenter.IsWindowOpen() && presenter.ProcessMessages()) {
        // Check keyboard input
        if (GetAsyncKeyState(VK_SPACE) & 1) {
            state.frameGenerationEnabled = !state.frameGenerationEnabled;
            std::cout << "Frame Generation: " << (state.frameGenerationEnabled ? "ON" : "OFF") << std::endl;
        }
        if (GetAsyncKeyState('S') & 1) {
            state.showStats = !state.showStats;
        }

        auto frameStart = std::chrono::high_resolution_clock::now();

        // Capture frame
        osfg::CapturedFrame capturedFrame;
        if (!capture.CaptureFrame(capturedFrame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Copy to interop (staged copy for cross-device)
        ComPtr<ID3D11Texture2D> srcTexture;
        capturedFrame.texture->QueryInterface(IID_PPV_ARGS(&srcTexture));
        interop.CopyFromD3D11Staged(capture.GetDevice(), capture.GetContext(), srcTexture.Get());
        capture.ReleaseFrame();

        state.framesProcessed++;

        // Reset command list
        commandAllocator->Reset();
        commandList->Reset(commandAllocator.Get(), nullptr);

        ID3D12Resource* frameToPresent = nullptr;

        // Process based on mode
        if (state.frameGenerationEnabled && interop.GetFrameCount() >= 2) {
            // Run optical flow
            opticalFlow.Dispatch(
                interop.GetCurrentFrameD3D12(),
                interop.GetPreviousFrameD3D12(),
                commandList.Get());

            // Run interpolation
            interpolation.Dispatch(
                interop.GetPreviousFrameD3D12(),
                interop.GetCurrentFrameD3D12(),
                opticalFlow.GetMotionVectorTexture(),
                commandList.Get());

            state.framesGenerated++;

            // Alternate between showing captured and generated frames
            // This simulates the 2x frame rate effect
            if (showNextFrame) {
                frameToPresent = interpolation.GetInterpolatedFrame();
            } else {
                frameToPresent = interop.GetCurrentFrameD3D12();
            }
            showNextFrame = !showNextFrame;
        } else {
            // Just show captured frame
            frameToPresent = interop.GetCurrentFrameD3D12();
        }

        // Present
        if (frameToPresent) {
            presenter.Present(frameToPresent, commandList.Get());
        }

        // Execute
        commandList->Close();
        ID3D12CommandList* lists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(1, lists);

        // Wait for GPU
        WaitForGPU(commandQueue.Get(), fence.Get(), fenceEvent, fenceValue);

        // Flip the swap chain to display the frame
        presenter.Flip(1, 0);

        // Swap buffers
        interop.SwapBuffers();

        // Calculate timing
        auto frameEnd = std::chrono::high_resolution_clock::now();
        double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        totalPipelineMs += frameMs;

        // Report statistics periodically
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<double>(now - lastReportTime).count() >= 1.0) {
            double elapsed = std::chrono::duration<double>(now - startTime).count();
            double fps = state.framesProcessed / elapsed;
            double effectiveFps = (state.framesProcessed + state.framesGenerated) / elapsed;
            double avgMs = totalPipelineMs / state.framesProcessed;

            if (state.showStats) {
                std::cout << "\rFPS: " << std::fixed << std::setprecision(1) << fps
                          << " -> " << effectiveFps << " (2x)"
                          << " | Pipeline: " << std::setprecision(2) << avgMs << "ms"
                          << " | Frames: " << state.framesProcessed
                          << " | Generated: " << state.framesGenerated
                          << "          " << std::flush;
            }
            lastReportTime = now;
        }
    }

    // Cleanup
    std::cout << "\n\n=== Demo Complete ===" << std::endl;
    std::cout << "Total frames processed: " << state.framesProcessed << std::endl;
    std::cout << "Total frames generated: " << state.framesGenerated << std::endl;

    if (fenceEvent) {
        CloseHandle(fenceEvent);
    }

    FreeConsole();
    return 0;
}
