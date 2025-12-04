// OSFG Simple Presenter Implementation
// Creates a window and displays frames using D3D12 swap chain
// MIT License - Part of Open Source Frame Generation project

#include "simple_presenter.h"
#include <chrono>

namespace OSFG {

// Window class name
static const wchar_t* WINDOW_CLASS_NAME = L"OSFG_Presenter";

SimplePresenter::SimplePresenter()
{
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_lastPresentTime);
}

SimplePresenter::~SimplePresenter()
{
    Shutdown();
}

bool SimplePresenter::Initialize(ID3D12Device* device,
                                  ID3D12CommandQueue* commandQueue,
                                  const PresenterConfig& config)
{
    if (m_initialized) {
        m_lastError = "Already initialized";
        return false;
    }

    if (!device || !commandQueue) {
        m_lastError = "Invalid D3D12 device or command queue";
        return false;
    }

    m_device = device;
    m_commandQueue = commandQueue;
    m_config = config;

    // Create components in order
    if (!CreatePresenterWindow()) return false;
    if (!CreateSwapChain()) return false;
    if (!CreateRenderTargets()) return false;
    if (!CreateSyncObjects()) return false;

    m_initialized = true;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    return true;
}

void SimplePresenter::Shutdown()
{
    if (!m_initialized) return;

    // Wait for GPU to finish
    WaitForGPU();

    // Close fence event
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    // Release resources
    for (int i = 0; i < MAX_BACK_BUFFERS; i++) {
        m_backBuffers[i].Reset();
    }
    m_fence.Reset();
    m_rtvHeap.Reset();
    m_swapChain.Reset();
    m_commandQueue.Reset();
    m_device.Reset();

    // Destroy window
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    // Unregister window class
    if (m_hinstance) {
        UnregisterClassW(WINDOW_CLASS_NAME, m_hinstance);
        m_hinstance = nullptr;
    }

    m_initialized = false;
}

bool SimplePresenter::IsWindowOpen() const
{
    return m_hwnd != nullptr && !m_windowClosed && IsWindow(m_hwnd);
}

bool SimplePresenter::ProcessMessages()
{
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_windowClosed = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !m_windowClosed;
}

bool SimplePresenter::Present(ID3D12Resource* sourceTexture,
                               ID3D12GraphicsCommandList* commandList)
{
    if (!m_initialized || !sourceTexture || !commandList) {
        m_lastError = "Invalid parameters or not initialized";
        return false;
    }

    // Get current back buffer
    ID3D12Resource* backBuffer = m_backBuffers[m_frameIndex].Get();

    // Transition source texture to copy source and back buffer to copy dest
    D3D12_RESOURCE_BARRIER barriers[2] = {};

    // Source texture: PIXEL_SHADER_RESOURCE/COMMON -> COPY_SOURCE
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = sourceTexture;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // Back buffer: PRESENT -> COPY_DEST
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = backBuffer;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(2, barriers);

    // Copy source texture to back buffer
    D3D12_TEXTURE_COPY_LOCATION destLoc = {};
    destLoc.pResource = backBuffer;
    destLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = sourceTexture;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_BOX srcBox = {};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = m_config.width;
    srcBox.bottom = m_config.height;
    srcBox.back = 1;

    commandList->CopyTextureRegion(&destLoc, 0, 0, 0, &srcLoc, &srcBox);

    // Transition both textures back
    // Source texture: COPY_SOURCE -> PIXEL_SHADER_RESOURCE
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Back buffer: COPY_DEST -> PRESENT
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    commandList->ResourceBarrier(2, barriers);

    return true;
}

ID3D12Resource* SimplePresenter::GetCurrentBackBuffer()
{
    return m_backBuffers[m_frameIndex].Get();
}

bool SimplePresenter::Flip(uint32_t syncInterval, uint32_t flags)
{
    if (!m_initialized || !m_swapChain) {
        m_lastError = "Not initialized";
        return false;
    }

    // Present the frame
    UINT presentFlags = flags;
    if (!m_config.vsync && syncInterval == 0) {
        presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    HRESULT hr = m_swapChain->Present(m_config.vsync ? 1 : syncInterval, presentFlags);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            m_lastError = "Device lost during present";
        } else {
            m_lastError = "Present failed: 0x" + std::to_string(hr);
        }
        return false;
    }

    // Update statistics
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    double presentTimeMs = 1000.0 * (currentTime.QuadPart - m_lastPresentTime.QuadPart) / m_frequency.QuadPart;
    m_lastPresentTime = currentTime;

    m_stats.lastPresentTimeMs = presentTimeMs;
    m_stats.framesPresented++;

    // Running average for FPS
    double alpha = 0.1;
    m_stats.avgPresentTimeMs = m_stats.avgPresentTimeMs * (1.0 - alpha) + presentTimeMs * alpha;
    if (m_stats.avgPresentTimeMs > 0) {
        m_stats.fps = 1000.0 / m_stats.avgPresentTimeMs;
    }

    // Schedule signal for fence
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), currentFenceValue);

    // Move to next frame
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Wait for next frame to be available
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    // Set fence value for next frame
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;

    return true;
}

bool SimplePresenter::CreatePresenterWindow()
{
    m_hinstance = GetModuleHandle(nullptr);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hinstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        m_lastError = "Failed to register window class";
        return false;
    }

    // Calculate window size for desired client area
    RECT rect = { 0, 0, (LONG)m_config.width, (LONG)m_config.height };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rect, style, FALSE);

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    // Center on screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    // Create window
    m_hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        m_config.windowTitle,
        style,
        x, y,
        windowWidth, windowHeight,
        nullptr,
        nullptr,
        m_hinstance,
        this  // Pass this pointer for WM_CREATE
    );

    if (!m_hwnd) {
        m_lastError = "Failed to create window";
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    return true;
}

bool SimplePresenter::CreateSwapChain()
{
    // Get DXGI factory
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        m_lastError = "Failed to create DXGI factory";
        return false;
    }

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_config.width;
    swapChainDesc.Height = m_config.height;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = m_config.bufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        m_hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create swap chain";
        return false;
    }

    // Disable Alt+Enter fullscreen
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Get IDXGISwapChain4 interface
    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr)) {
        m_lastError = "Failed to get IDXGISwapChain4";
        return false;
    }

    return true;
}

bool SimplePresenter::CreateRenderTargets()
{
    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = m_config.bufferCount;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr)) {
        m_lastError = "Failed to create RTV descriptor heap";
        return false;
    }

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create render target views
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < m_config.bufferCount; i++) {
        hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        if (FAILED(hr)) {
            m_lastError = "Failed to get swap chain buffer " + std::to_string(i);
            return false;
        }

        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    return true;
}

bool SimplePresenter::CreateSyncObjects()
{
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        m_lastError = "Failed to create fence";
        return false;
    }

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        m_lastError = "Failed to create fence event";
        return false;
    }

    // Initialize fence values
    for (uint32_t i = 0; i < m_config.bufferCount; i++) {
        m_fenceValues[i] = 0;
    }

    return true;
}

void SimplePresenter::WaitForGPU()
{
    if (!m_fence || !m_commandQueue) return;

    // Signal and wait for fence
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), currentFenceValue);

    if (m_fence->GetCompletedValue() < currentFenceValue) {
        m_fence->SetEventOnCompletion(currentFenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_fenceValues[m_frameIndex]++;
}

LRESULT CALLBACK SimplePresenter::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SimplePresenter* presenter = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        presenter = reinterpret_cast<SimplePresenter*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(presenter));
    } else {
        presenter = reinterpret_cast<SimplePresenter*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
        case WM_CLOSE:
            if (presenter) {
                presenter->m_windowClosed = true;
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (presenter) {
                    presenter->m_windowClosed = true;
                }
                PostQuitMessage(0);
                return 0;
            }
            break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace OSFG
