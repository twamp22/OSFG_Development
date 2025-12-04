// OSFG Interop Display Test
// Tests capture -> interop -> presenter pipeline without optical flow
// Used to isolate display issues

#include <iostream>
#include <chrono>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "capture/dxgi_capture.h"
#include "interop/d3d11_d3d12_interop.h"
#include "presentation/simple_presenter.h"

using Microsoft::WRL::ComPtr;

bool CreateD3D12Device(ComPtr<ID3D12Device>& device, ComPtr<ID3D12CommandQueue>& commandQueue) {
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    return SUCCEEDED(hr);
}

void WaitForGPU(ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE event, UINT64& value) {
    value++;
    queue->Signal(fence, value);
    if (fence->GetCompletedValue() < value) {
        fence->SetEventOnCompletion(value, event);
        WaitForSingleObject(event, INFINITE);
    }
}

int main() {
    std::cout << "=== OSFG Interop Display Test ===\n";
    std::cout << "Tests: Capture -> Interop -> Presenter (no optical flow)\n\n";

    // Create D3D12 device
    std::cout << "[1/4] Creating D3D12 device...\n";
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> cmdQueue;
    if (!CreateD3D12Device(device, cmdQueue)) {
        std::cerr << "Failed to create D3D12 device\n";
        return 1;
    }

    // Create command list and fence
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
    std::cout << "      Done.\n";

    // Initialize capture
    std::cout << "[2/4] Initializing capture...\n";
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

    // Initialize interop
    std::cout << "[3/4] Initializing interop...\n";
    OSFG::D3D11D3D12Interop interop;
    OSFG::InteropConfig interopConfig;
    interopConfig.width = width;
    interopConfig.height = height;
    interopConfig.format = DXGI_FORMAT_B8G8R8A8_UNORM;

    if (!interop.Initialize(device.Get(), cmdQueue.Get(), interopConfig)) {
        std::cerr << "Failed: " << interop.GetLastError() << "\n";
        return 1;
    }
    std::cout << "      Done.\n";

    // Initialize presenter
    std::cout << "[4/4] Initializing presenter...\n";
    OSFG::SimplePresenter presenter;
    OSFG::PresenterConfig presConfig;
    presConfig.width = width > 1280 ? 1280 : width;
    presConfig.height = height > 720 ? 720 : height;
    presConfig.vsync = true;
    presConfig.windowTitle = L"OSFG Interop Test";

    if (!presenter.Initialize(device.Get(), cmdQueue.Get(), presConfig)) {
        std::cerr << "Failed: " << presenter.GetLastError() << "\n";
        return 1;
    }
    std::cout << "      Window: " << presConfig.width << "x" << presConfig.height << "\n\n";

    std::cout << "Running... Press ESC to exit.\n\n";

    int frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (presenter.IsWindowOpen() && presenter.ProcessMessages()) {
        // Capture frame
        osfg::CapturedFrame frame;
        if (!capture.CaptureFrame(frame)) {
            Sleep(1);
            continue;
        }

        // Copy to interop using staged copy
        ComPtr<ID3D11Texture2D> srcTex;
        frame.texture->QueryInterface(IID_PPV_ARGS(&srcTex));

        if (!interop.CopyFromD3D11Staged(capture.GetDevice(), capture.GetContext(), srcTex.Get())) {
            std::cerr << "Interop copy failed: " << interop.GetLastError() << "\n";
            capture.ReleaseFrame();
            continue;
        }
        capture.ReleaseFrame();

        // Get the D3D12 texture from interop
        ID3D12Resource* interopTexture = interop.GetCurrentFrameD3D12();

        // Debug: Print texture info on first frame
        if (frameCount == 0) {
            D3D12_RESOURCE_DESC desc = interopTexture->GetDesc();
            std::cout << "Interop texture: " << desc.Width << "x" << desc.Height
                      << " format=" << desc.Format << "\n";
        }

        // Reset command list
        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

        // Present the interop texture
        if (!presenter.Present(interopTexture, cmdList.Get())) {
            std::cerr << "Present failed: " << presenter.GetLastError() << "\n";
        }

        // Execute
        cmdList->Close();
        ID3D12CommandList* lists[] = { cmdList.Get() };
        cmdQueue->ExecuteCommandLists(1, lists);

        // Wait for GPU
        WaitForGPU(cmdQueue.Get(), fence.Get(), fenceEvent, fenceValue);

        // Flip
        if (!presenter.Flip(1, 0)) {
            std::cerr << "Flip failed: " << presenter.GetLastError() << "\n";
        }

        // Swap interop buffers
        interop.SwapBuffers();

        frameCount++;

        // Print FPS
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        if (elapsed >= 1.0) {
            std::cout << "FPS: " << (frameCount / elapsed) << "\n";
            frameCount = 0;
            startTime = now;
        }
    }

    CloseHandle(fenceEvent);
    std::cout << "\nTest complete.\n";
    return 0;
}
