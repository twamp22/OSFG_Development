// OSFG Minimal Capture and Display Test
// Simply captures the screen and displays it - no frame generation
// Used to diagnose display issues

#include <iostream>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "capture/dxgi_capture.h"

using Microsoft::WRL::ComPtr;

// Simple window for display
HWND g_hwnd = nullptr;
bool g_running = true;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY || msg == WM_CLOSE) {
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool CreateTestWindow(int width, int height) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"OSFGCaptureTest";
    RegisterClassExW(&wc);

    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExW(0, L"OSFGCaptureTest", L"OSFG Capture Display Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_hwnd) return false;
    ShowWindow(g_hwnd, SW_SHOW);
    return true;
}

int main() {
    std::cout << "=== OSFG Minimal Capture Display Test ===\n";
    std::cout << "This test captures your screen and displays it directly.\n";
    std::cout << "You should see your desktop mirrored in a window.\n";
    std::cout << "Press ESC to exit.\n\n";

    // Initialize capture
    std::cout << "[1/4] Initializing DXGI capture...\n";
    osfg::DXGICapture capture;
    osfg::CaptureConfig captureConfig;
    captureConfig.outputIndex = 0;
    captureConfig.timeoutMs = 100;
    captureConfig.createStagingTexture = true;  // We need CPU access

    if (!capture.Initialize(captureConfig)) {
        std::cerr << "Failed to init capture: " << capture.GetLastError() << "\n";
        return 1;
    }

    uint32_t width = capture.GetWidth();
    uint32_t height = capture.GetHeight();
    std::cout << "      Capture ready: " << width << "x" << height << "\n";

    // Create D3D12 device
    std::cout << "[2/4] Creating D3D12 device...\n";
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> cmdQueue;
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D12 device\n";
        return 1;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    cmdList->Close();
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    std::cout << "      D3D12 ready.\n";

    // Create window
    std::cout << "[3/4] Creating display window...\n";
    int windowWidth = width > 1280 ? 1280 : width;
    int windowHeight = height > 720 ? 720 : height;
    if (!CreateTestWindow(windowWidth, windowHeight)) {
        std::cerr << "Failed to create window\n";
        return 1;
    }
    std::cout << "      Window created: " << windowWidth << "x" << windowHeight << "\n";

    // Create swap chain
    std::cout << "[4/4] Creating swap chain...\n";
    ComPtr<IDXGIFactory4> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = windowWidth;
    scDesc.Height = windowHeight;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Match capture format
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    factory->CreateSwapChainForHwnd(cmdQueue.Get(), g_hwnd, &scDesc, nullptr, nullptr, &swapChain1);
    ComPtr<IDXGISwapChain4> swapChain;
    swapChain1.As(&swapChain);

    // Get back buffers
    ComPtr<ID3D12Resource> backBuffers[2];
    for (int i = 0; i < 2; i++) {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
    }

    // Create upload buffer for copying captured data
    UINT rowPitch = (windowWidth * 4 + 255) & ~255;  // 256-byte aligned
    UINT uploadSize = rowPitch * windowHeight;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

    void* uploadPtr = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    uploadBuffer->Map(0, &readRange, &uploadPtr);

    std::cout << "      Swap chain ready.\n\n";
    std::cout << "Starting capture loop... (Press ESC to exit)\n\n";

    // Main loop
    int frameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_running) break;

        // Capture frame
        osfg::CapturedFrame frame;
        if (!capture.CaptureFrame(frame)) {
            Sleep(1);
            continue;
        }

        // Get D3D11 staging texture and map it
        ID3D11DeviceContext* ctx = capture.GetContext();
        ComPtr<ID3D11Texture2D> stagingTex;

        // Create staging texture for CPU read
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = width;
        stagingDesc.Height = height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ComPtr<ID3D11Texture2D> srcTex;
        frame.texture->QueryInterface(IID_PPV_ARGS(&srcTex));

        capture.GetDevice()->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);
        ctx->CopyResource(stagingTex.Get(), srcTex.Get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = ctx->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            // Copy to upload buffer (with scaling if needed)
            BYTE* src = (BYTE*)mapped.pData;
            BYTE* dst = (BYTE*)uploadPtr;

            float scaleX = (float)width / windowWidth;
            float scaleY = (float)height / windowHeight;

            for (int y = 0; y < windowHeight; y++) {
                int srcY = (int)(y * scaleY);
                if (srcY >= (int)height) srcY = height - 1;

                for (int x = 0; x < windowWidth; x++) {
                    int srcX = (int)(x * scaleX);
                    if (srcX >= (int)width) srcX = width - 1;

                    BYTE* srcPixel = src + srcY * mapped.RowPitch + srcX * 4;
                    BYTE* dstPixel = dst + y * rowPitch + x * 4;

                    dstPixel[0] = srcPixel[0];  // B
                    dstPixel[1] = srcPixel[1];  // G
                    dstPixel[2] = srcPixel[2];  // R
                    dstPixel[3] = srcPixel[3];  // A
                }
            }
            ctx->Unmap(stagingTex.Get(), 0);
        }

        capture.ReleaseFrame();

        // Copy upload buffer to back buffer
        UINT frameIndex = swapChain->GetCurrentBackBufferIndex();

        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

        // Transition back buffer to copy dest
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffers[frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);

        // Copy from upload buffer to back buffer
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = backBuffers[frameIndex].Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = uploadBuffer.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = 0;
        srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srcLoc.PlacedFootprint.Footprint.Width = windowWidth;
        srcLoc.PlacedFootprint.Footprint.Height = windowHeight;
        srcLoc.PlacedFootprint.Footprint.Depth = 1;
        srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

        cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        // Transition back buffer to present
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmdList->ResourceBarrier(1, &barrier);

        cmdList->Close();

        ID3D12CommandList* lists[] = { cmdList.Get() };
        cmdQueue->ExecuteCommandLists(1, lists);

        // Present
        swapChain->Present(1, 0);

        // Wait for GPU
        fenceValue++;
        cmdQueue->Signal(fence.Get(), fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        frameCount++;

        // Print FPS every second
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        if (elapsed >= 1.0) {
            std::cout << "FPS: " << (frameCount / elapsed) << "\n";
            frameCount = 0;
            startTime = now;
        }
    }

    // Cleanup
    if (uploadPtr) uploadBuffer->Unmap(0, nullptr);
    if (fenceEvent) CloseHandle(fenceEvent);
    DestroyWindow(g_hwnd);

    std::cout << "\nTest complete.\n";
    return 0;
}
