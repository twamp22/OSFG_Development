// OSFG Optical Flow Only Test
// Tests capture + optical flow dispatch (no interpolation)
// If this freezes, issue is in optical flow. If it works, issue is in interpolation.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <iostream>
#include <chrono>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "capture/dxgi_capture.h"
#include "interop/d3d11_d3d12_interop.h"
#include "presentation/simple_presenter.h"
#include "opticalflow/simple_opticalflow.h"

using Microsoft::WRL::ComPtr;

bool CreateD3D12Device(ComPtr<ID3D12Device>& device, ComPtr<ID3D12CommandQueue>& commandQueue) {
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    return SUCCEEDED(hr);
}

void WaitForGPU(ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE fenceEvent, UINT64& fenceValue) {
    fenceValue++;
    queue->Signal(fence, fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

int main() {
    std::cout << "=== OSFG Optical Flow Only Test ===\n";
    std::cout << "Tests: Capture -> Interop -> Optical Flow -> Present (NO interpolation)\n";
    std::cout << "If this freezes, issue is in optical flow.\n";
    std::cout << "If this works, issue is in interpolation.\n";
    std::cout << "Press ESC to exit\n\n";

    // D3D12
    std::cout << "[1/5] Creating D3D12 device...\n";
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> cmdQueue;
    if (!CreateD3D12Device(device, cmdQueue)) {
        std::cerr << "Failed to create D3D12 device\n";
        return 1;
    }

    // Capture
    std::cout << "[2/5] Initializing capture...\n";
    osfg::DXGICapture capture;
    osfg::CaptureConfig captureConfig;
    captureConfig.outputIndex = 0;
    captureConfig.timeoutMs = 100;
    if (!capture.Initialize(captureConfig)) {
        std::cerr << "Failed: " << capture.GetLastError() << "\n";
        return 1;
    }
    uint32_t width = capture.GetWidth();
    uint32_t height = capture.GetHeight();
    std::cout << "      Resolution: " << width << "x" << height << "\n";

    // Interop
    std::cout << "[3/5] Initializing interop...\n";
    OSFG::D3D11D3D12Interop interop;
    OSFG::InteropConfig interopConfig;
    interopConfig.width = width;
    interopConfig.height = height;
    interopConfig.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!interop.Initialize(device.Get(), cmdQueue.Get(), interopConfig)) {
        std::cerr << "Failed: " << interop.GetLastError() << "\n";
        return 1;
    }

    // Optical Flow
    std::cout << "[4/5] Initializing optical flow...\n";
    OSFG::SimpleOpticalFlow opticalFlow;
    OSFG::SimpleOpticalFlowConfig ofConfig;
    ofConfig.width = width;
    ofConfig.height = height;
    ofConfig.blockSize = 8;
    ofConfig.searchRadius = 4;  // Reduced for better performance
    if (!opticalFlow.Initialize(device.Get(), ofConfig)) {
        std::cerr << "Failed: " << opticalFlow.GetLastError() << "\n";
        return 1;
    }
    std::cout << "      Motion vectors: " << opticalFlow.GetMotionVectorWidth() << "x"
              << opticalFlow.GetMotionVectorHeight() << "\n";

    // Presenter
    std::cout << "[5/5] Creating window...\n";
    OSFG::SimplePresenter presenter;
    OSFG::PresenterConfig presConfig;
    presConfig.width = width > 1280 ? 1280 : width;
    presConfig.height = height > 720 ? 720 : height;
    presConfig.vsync = true;
    presConfig.windowTitle = L"OSFG Optical Flow Test";
    if (!presenter.Initialize(device.Get(), cmdQueue.Get(), presConfig)) {
        std::cerr << "Failed: " << presenter.GetLastError() << "\n";
        return 1;
    }
    std::cout << "      Window: " << presConfig.width << "x" << presConfig.height << "\n\n";

    // Command list and fence
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent;
    UINT64 fenceValue = 0;

    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    cmdList->Close();
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    std::cout << "Running optical flow (but only presenting captured frames)...\n\n";

    int frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (presenter.IsWindowOpen() && presenter.ProcessMessages()) {
        // Capture
        osfg::CapturedFrame frame;
        if (!capture.CaptureFrame(frame)) {
            Sleep(1);
            continue;
        }

        // Copy to interop
        ComPtr<ID3D11Texture2D> srcTex;
        frame.texture->QueryInterface(IID_PPV_ARGS(&srcTex));
        interop.CopyFromD3D11Staged(capture.GetDevice(), capture.GetContext(), srcTex.Get());
        capture.ReleaseFrame();

        // Reset command list
        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

        // Run optical flow if we have 2+ frames (but don't use the output)
        if (interop.GetFrameCount() >= 2) {
            opticalFlow.Dispatch(
                interop.GetCurrentFrameD3D12(),
                interop.GetPreviousFrameD3D12(),
                cmdList.Get());
        }

        // Always present the captured frame (not interpolated)
        ID3D12Resource* tex = interop.GetCurrentFrameD3D12();
        presenter.Present(tex, cmdList.Get());

        cmdList->Close();
        ID3D12CommandList* lists[] = { cmdList.Get() };
        cmdQueue->ExecuteCommandLists(1, lists);

        WaitForGPU(cmdQueue.Get(), fence.Get(), fenceEvent, fenceValue);
        presenter.Flip(1, 0);
        interop.SwapBuffers();

        frameCount++;

        // Print FPS
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        if (elapsed >= 1.0) {
            std::cout << "FPS: " << (frameCount / elapsed)
                      << " | OF dispatches: " << opticalFlow.GetStats().framesProcessed << "\n";
            frameCount = 0;
            startTime = now;
        }
    }

    CloseHandle(fenceEvent);
    std::cout << "\nTest complete.\n";
    return 0;
}
