// OSFG - Open Source Frame Generation
// FidelityFX Frame Generation Wrapper Implementation

#include "ffx_framegen.h"
#include "ffx_loader.h"

// DX12-specific frame generation descriptor type IDs
// These match the FidelityFX SDK's ffx_api_framegeneration_dx12.h
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12 0x30006u
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12 0x30001u
#define FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12 0x30007u

namespace {

// Swap chain creation descriptor for HWND
// Layout must match ffxCreateContextDescFrameGenerationSwapChainForHwndDX12
struct FFXSwapChainForHwndDesc {
    OSFG::FFXApiHeader header;
    IDXGISwapChain4** swapchain;
    HWND hwnd;
    DXGI_SWAP_CHAIN_DESC1* desc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc;
    IDXGIFactory* dxgiFactory;
    ID3D12CommandQueue* gameQueue;
};

// Swap chain wrap descriptor
// Layout must match ffxCreateContextDescFrameGenerationSwapChainWrapDX12
struct FFXSwapChainWrapDesc {
    OSFG::FFXApiHeader header;
    IDXGISwapChain4** swapchain;
    ID3D12CommandQueue* gameQueue;
};

// Wait for presents dispatch descriptor
// Layout must match ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12
struct FFXWaitForPresentsDesc {
    OSFG::FFXApiHeader header;
};

} // anonymous namespace

namespace OSFG {

FFXFrameGeneration::FFXFrameGeneration() {
    QueryPerformanceFrequency(&m_frequency);
}

FFXFrameGeneration::~FFXFrameGeneration() {
    Shutdown();
}

bool FFXFrameGeneration::Initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    IDXGIFactory4* dxgiFactory,
    HWND hwnd,
    const FFXFrameGenConfig& config
) {
    if (m_initialized) {
        m_lastError = "Already initialized";
        return false;
    }

    // Check FFX availability
    FFXLoader& loader = FFXLoader::Instance();
    if (!loader.IsLoaded() && !loader.Load()) {
        m_lastError = "Failed to load FidelityFX: " + loader.GetLastError();
        return false;
    }

    // Store references
    m_device = device;
    m_commandQueue = commandQueue;
    m_dxgiFactory = dxgiFactory;
    m_config = config;

    // Create FFX swap chain
    if (!CreateSwapChainContext(hwnd, config)) {
        return false;
    }

    m_ownsSwapChain = true;
    m_initialized = true;
    QueryPerformanceCounter(&m_lastPresentTime);

    return true;
}

bool FFXFrameGeneration::InitializeWithSwapChain(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    IDXGISwapChain4* existingSwapChain
) {
    if (m_initialized) {
        m_lastError = "Already initialized";
        return false;
    }

    // Check FFX availability
    FFXLoader& loader = FFXLoader::Instance();
    if (!loader.IsLoaded() && !loader.Load()) {
        m_lastError = "Failed to load FidelityFX: " + loader.GetLastError();
        return false;
    }

    // Store references
    m_device = device;
    m_commandQueue = commandQueue;

    // Wrap existing swap chain
    if (!WrapExistingSwapChain(existingSwapChain)) {
        return false;
    }

    m_ownsSwapChain = false;
    m_initialized = true;
    QueryPerformanceCounter(&m_lastPresentTime);

    return true;
}

bool FFXFrameGeneration::CreateSwapChainContext(HWND hwnd, const FFXFrameGenConfig& config) {
    FFXLoader& loader = FFXLoader::Instance();

    // Create swap chain description
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = config.displayWidth;
    swapChainDesc.Height = config.displayHeight;
    swapChainDesc.Format = config.backBufferFormat;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = config.backBufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    // Setup FFX create context descriptor
    FFXSwapChainForHwndDesc createDesc = {};
    createDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    createDesc.header.pNext = nullptr;

    IDXGISwapChain4* pSwapChain = nullptr;
    createDesc.swapchain = &pSwapChain;
    createDesc.hwnd = hwnd;
    createDesc.desc = &swapChainDesc;
    createDesc.fullscreenDesc = nullptr;
    createDesc.dxgiFactory = m_dxgiFactory.Get();
    createDesc.gameQueue = m_commandQueue.Get();

    // Create the FFX context (this creates the swap chain internally)
    ffxReturnCode_t result = loader.CreateContext(
        &m_ffxContext,
        &createDesc.header,
        nullptr  // Use default allocator
    );

    if (!FFXLoader::Succeeded(result)) {
        char errorMsg[256];
        sprintf_s(errorMsg, "ffxCreateContext failed with code %u", result);
        m_lastError = errorMsg;
        return false;
    }

    // Store the swap chain
    m_swapChain = pSwapChain;

    return true;
}

bool FFXFrameGeneration::WrapExistingSwapChain(IDXGISwapChain4* swapChain) {
    FFXLoader& loader = FFXLoader::Instance();

    // Setup wrap descriptor
    FFXSwapChainWrapDesc wrapDesc = {};
    wrapDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12;
    wrapDesc.header.pNext = nullptr;

    IDXGISwapChain4* pSwapChain = swapChain;
    wrapDesc.swapchain = &pSwapChain;
    wrapDesc.gameQueue = m_commandQueue.Get();

    // Create the FFX context (wraps the swap chain)
    ffxReturnCode_t result = loader.CreateContext(
        &m_ffxContext,
        &wrapDesc.header,
        nullptr
    );

    if (!FFXLoader::Succeeded(result)) {
        char errorMsg[256];
        sprintf_s(errorMsg, "ffxCreateContext (wrap) failed with code %u", result);
        m_lastError = errorMsg;
        return false;
    }

    // The wrapped swap chain may be different
    m_swapChain = pSwapChain;

    return true;
}

void FFXFrameGeneration::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Wait for pending presents
    WaitForPendingPresents();

    // Destroy FFX context
    if (m_ffxContext) {
        FFXLoader& loader = FFXLoader::Instance();
        if (loader.IsLoaded()) {
            loader.DestroyContext(&m_ffxContext, nullptr);
        }
        m_ffxContext = nullptr;
    }

    // Release swap chain
    m_swapChain.Reset();
    m_commandQueue.Reset();
    m_dxgiFactory.Reset();
    m_device.Reset();

    m_initialized = false;
    m_enabled = true;
}

bool FFXFrameGeneration::Configure(const FFXFrameGenConfig& config) {
    if (!m_initialized) {
        m_lastError = "Not initialized";
        return false;
    }

    m_config = config;
    // TODO: Apply configuration to FFX context if needed

    return true;
}

bool FFXFrameGeneration::SetEnabled(bool enabled) {
    if (!m_initialized) {
        m_lastError = "Not initialized";
        return false;
    }

    m_enabled = enabled;
    // TODO: Configure FFX context to enable/disable frame generation

    return true;
}

bool FFXFrameGeneration::Present(uint32_t syncInterval, uint32_t flags) {
    if (!m_initialized || !m_swapChain) {
        m_lastError = "Not initialized";
        return false;
    }

    // Present through the FFX-wrapped swap chain
    // FFX handles frame generation and pacing internally
    HRESULT hr = m_swapChain->Present(
        m_config.vsync ? 1 : syncInterval,
        m_config.vsync ? 0 : (flags | DXGI_PRESENT_ALLOW_TEARING)
    );

    if (FAILED(hr)) {
        char errorMsg[256];
        sprintf_s(errorMsg, "Swap chain Present failed: 0x%08X", static_cast<unsigned int>(hr));
        m_lastError = errorMsg;
        return false;
    }

    // Update statistics
    UpdateStats();
    m_stats.framesPresented++;

    return true;
}

void FFXFrameGeneration::WaitForPendingPresents() {
    if (!m_initialized || !m_ffxContext) {
        return;
    }

    FFXLoader& loader = FFXLoader::Instance();
    if (!loader.IsLoaded()) {
        return;
    }

    // Dispatch wait for presents
    FFXWaitForPresentsDesc waitDesc = {};
    waitDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
    waitDesc.header.pNext = nullptr;

    loader.Dispatch(
        &m_ffxContext,
        &waitDesc.header
    );
}

void FFXFrameGeneration::UpdateStats() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    // Calculate frame time
    float deltaMs = static_cast<float>(now.QuadPart - m_lastPresentTime.QuadPart) * 1000.0f /
                    static_cast<float>(m_frequency.QuadPart);

    m_stats.lastFrameTimeMs = deltaMs;

    // Update running average (simple exponential moving average)
    const float alpha = 0.1f;
    m_stats.averageFrameTimeMs = m_stats.averageFrameTimeMs * (1.0f - alpha) + deltaMs * alpha;

    m_lastPresentTime = now;
}

} // namespace OSFG
