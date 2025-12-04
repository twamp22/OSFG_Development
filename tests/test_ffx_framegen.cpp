// OSFG - Open Source Frame Generation
// FidelityFX Frame Generation Wrapper Test
//
// Tests the FFX frame generation wrapper functionality

#include "ffx/ffx_loader.h"
#include "ffx/ffx_framegen.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;

// Simple window class for testing
LRESULT CALLBACK TestWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool CreateTestWindow(HWND& hwnd, int width, int height) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = TestWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"OSFGTestWindow";

    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(
        0,
        L"OSFGTestWindow",
        L"OSFG FFX Frame Generation Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!hwnd) {
        printf("Failed to create window\n");
        return false;
    }

    ShowWindow(hwnd, SW_SHOW);
    return true;
}

bool CreateD3D12Device(
    ComPtr<IDXGIFactory4>& factory,
    ComPtr<ID3D12Device>& device,
    ComPtr<ID3D12CommandQueue>& commandQueue
) {
    // Create DXGI factory
    UINT factoryFlags = 0;
#ifdef _DEBUG
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        printf("Failed to create DXGI factory: 0x%08X\n", static_cast<unsigned int>(hr));
        return false;
    }

    // Find a suitable adapter
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Try to create device
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
        if (SUCCEEDED(hr)) {
            wprintf(L"Using adapter: %s\n", desc.Description);
            break;
        }
    }

    if (!device) {
        printf("Failed to create D3D12 device\n");
        return false;
    }

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) {
        printf("Failed to create command queue: 0x%08X\n", static_cast<unsigned int>(hr));
        return false;
    }

    return true;
}

int main() {
    printf("=== OSFG FidelityFX Frame Generation Test ===\n\n");

    // Check if FFX is available
    printf("[1/5] Checking FidelityFX availability...\n");
    if (!OSFG::FFXLoader::IsAvailable()) {
        printf("      FidelityFX DLLs NOT found.\n");
        printf("      This test requires FidelityFX DLLs.\n");
        return 1;
    }
    printf("      FidelityFX DLLs are available!\n\n");

    // Load FFX
    printf("[2/5] Loading FidelityFX...\n");
    OSFG::FFXLoader& loader = OSFG::FFXLoader::Instance();
    if (!loader.Load()) {
        printf("      FAILED: %s\n", loader.GetLastError().c_str());
        return 1;
    }
    printf("      Loaded successfully!\n\n");

    // Create test window
    printf("[3/5] Creating test window...\n");
    HWND hwnd = nullptr;
    const int WIDTH = 800;
    const int HEIGHT = 600;
    if (!CreateTestWindow(hwnd, WIDTH, HEIGHT)) {
        return 1;
    }
    printf("      Window created: %dx%d\n\n", WIDTH, HEIGHT);

    // Create D3D12 device
    printf("[4/5] Creating D3D12 device...\n");
    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    if (!CreateD3D12Device(factory, device, commandQueue)) {
        DestroyWindow(hwnd);
        return 1;
    }
    printf("      D3D12 device created!\n\n");

    // Initialize FFX frame generation
    printf("[5/5] Initializing FFX frame generation...\n");
    OSFG::FFXFrameGeneration ffxFrameGen;

    OSFG::FFXFrameGenConfig config;
    config.displayWidth = WIDTH;
    config.displayHeight = HEIGHT;
    config.backBufferCount = 3;
    config.backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    config.vsync = false;

    bool initResult = ffxFrameGen.Initialize(
        device.Get(),
        commandQueue.Get(),
        factory.Get(),
        hwnd,
        config
    );

    if (!initResult) {
        printf("      FAILED: %s\n", ffxFrameGen.GetLastError().c_str());
        printf("\n=== FFX Frame Generation Test FAILED ===\n");
        printf("\nNote: FFX frame generation requires specific GPU support.\n");
        printf("This is expected on systems without AMD GPU with FSR 3 support.\n");
        DestroyWindow(hwnd);
        return 1;
    }

    printf("      FFX frame generation initialized!\n");
    printf("      Swap chain: %p\n", static_cast<void*>(ffxFrameGen.GetSwapChain()));

    // Test a few presents
    printf("\nTesting frame presentation...\n");
    const int TEST_FRAMES = 10;
    for (int i = 0; i < TEST_FRAMES; i++) {
        // Process window messages
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Present
        if (!ffxFrameGen.Present(0, 0)) {
            printf("  Frame %d: Present FAILED: %s\n", i + 1, ffxFrameGen.GetLastError().c_str());
        } else {
            const auto& stats = ffxFrameGen.GetStats();
            printf("  Frame %d: Presented (%.2f ms)\n", i + 1, stats.lastFrameTimeMs);
        }

        Sleep(16); // ~60 FPS
    }

    // Print final stats
    const auto& stats = ffxFrameGen.GetStats();
    printf("\nFinal Statistics:\n");
    printf("  Frames presented: %llu\n", stats.framesPresented);
    printf("  Average frame time: %.2f ms\n", stats.averageFrameTimeMs);

    // Shutdown
    printf("\nShutting down...\n");
    ffxFrameGen.Shutdown();
    DestroyWindow(hwnd);

    printf("\n=== FFX Frame Generation Test PASSED ===\n");
    return 0;
}
