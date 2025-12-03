// OSFG - Open Source Frame Generation
// DXGI Desktop Duplication Capture Engine Implementation

#include "dxgi_capture.h"
#include <dxgi1_6.h>
#include <sstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace osfg {

DXGICapture::DXGICapture() = default;

DXGICapture::~DXGICapture() {
    Shutdown();
}

bool DXGICapture::Initialize(const CaptureConfig& config) {
    if (m_initialized) {
        Shutdown();
    }

    m_config = config;

    // Create D3D11 device
    if (!CreateD3D11Device(config.adapterIndex)) {
        return false;
    }

    // Initialize desktop duplication
    if (!InitializeDesktopDuplication(config.outputIndex)) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    ResetStats();
    return true;
}

bool DXGICapture::Initialize(ID3D11Device* externalDevice, const CaptureConfig& config) {
    if (m_initialized) {
        Shutdown();
    }

    if (!externalDevice) {
        SetError("External device is null");
        return false;
    }

    m_config = config;

    // Use the external device
    m_device = externalDevice;
    externalDevice->GetImmediateContext(&m_context);

    // Initialize desktop duplication
    if (!InitializeDesktopDuplication(config.outputIndex)) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    ResetStats();
    return true;
}

void DXGICapture::Shutdown() {
    if (m_frameAcquired && m_duplication) {
        m_duplication->ReleaseFrame();
        m_frameAcquired = false;
    }

    m_stagingTexture.Reset();
    m_duplication.Reset();
    m_context.Reset();
    m_device.Reset();

    m_initialized = false;
    m_width = 0;
    m_height = 0;
}

bool DXGICapture::CreateD3D11Device(uint32_t adapterIndex) {
    HRESULT hr;

    // Create DXGI factory
    ComPtr<IDXGIFactory1> factory;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        SetError("Failed to create DXGI factory");
        return false;
    }

    // Get the specified adapter
    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(adapterIndex, &adapter);
    if (FAILED(hr)) {
        SetError("Failed to get adapter " + std::to_string(adapterIndex));
        return false;
    }

    // Log adapter info
    DXGI_ADAPTER_DESC1 adapterDesc;
    adapter->GetDesc1(&adapterDesc);

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );

    if (FAILED(hr)) {
        SetError("Failed to create D3D11 device");
        return false;
    }

    return true;
}

bool DXGICapture::InitializeDesktopDuplication(uint32_t outputIndex) {
    HRESULT hr;

    // Get DXGI device
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) {
        SetError("Failed to get DXGI device");
        return false;
    }

    // Get adapter
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        SetError("Failed to get adapter");
        return false;
    }

    // Get output (monitor)
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(outputIndex, &output);
    if (FAILED(hr)) {
        SetError("Failed to get output " + std::to_string(outputIndex));
        return false;
    }

    // Get output1 interface for desktop duplication
    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        SetError("Failed to get IDXGIOutput1 interface");
        return false;
    }

    // Get output description for dimensions
    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);
    m_width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    m_height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    // Create desktop duplication
    hr = output1->DuplicateOutput(m_device.Get(), &m_duplication);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            SetError("Desktop duplication not available - another app may be using it");
        } else if (hr == E_ACCESSDENIED) {
            SetError("Access denied - running as admin may help");
        } else {
            std::stringstream ss;
            ss << "Failed to create desktop duplication (0x" << std::hex << hr << ")";
            SetError(ss.str());
        }
        return false;
    }

    // Create staging texture if requested (for CPU readback)
    if (m_config.createStagingTexture) {
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = m_width;
        stagingDesc.Height = m_height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
        if (FAILED(hr)) {
            SetError("Failed to create staging texture");
            return false;
        }
    }

    return true;
}

bool DXGICapture::CaptureFrame(CapturedFrame& outFrame) {
    if (!m_initialized) {
        SetError("Not initialized");
        return false;
    }

    // Release previous frame if still held
    if (m_frameAcquired) {
        m_duplication->ReleaseFrame();
        m_frameAcquired = false;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Acquire next frame
    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};

    HRESULT hr = m_duplication->AcquireNextFrame(
        m_config.timeoutMs,
        &frameInfo,
        &desktopResource
    );

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // No new frame available
        outFrame.isValid = false;
        return false;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // Desktop duplication lost - need to reinitialize
        SetError("Desktop duplication access lost - reinitialize required");
        m_initialized = false;
        return false;
    }

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to acquire frame (0x" << std::hex << hr << ")";
        SetError(ss.str());
        m_stats.framesMissed++;
        return false;
    }

    m_frameAcquired = true;

    // Get the texture
    ComPtr<ID3D11Texture2D> texture;
    hr = desktopResource.As(&texture);
    if (FAILED(hr)) {
        SetError("Failed to get texture from desktop resource");
        m_duplication->ReleaseFrame();
        m_frameAcquired = false;
        m_stats.framesMissed++;
        return false;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double captureTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Update statistics
    m_stats.framesCapture++;
    m_stats.lastCaptureTimeMs = captureTimeMs;
    m_stats.minCaptureTimeMs = (std::min)(m_stats.minCaptureTimeMs, captureTimeMs);
    m_stats.maxCaptureTimeMs = (std::max)(m_stats.maxCaptureTimeMs, captureTimeMs);

    // Running average
    double alpha = 0.1;
    m_stats.avgCaptureTimeMs = m_stats.avgCaptureTimeMs * (1.0 - alpha) + captureTimeMs * alpha;

    // Fill output frame
    outFrame.texture = texture;
    outFrame.width = m_width;
    outFrame.height = m_height;
    outFrame.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    outFrame.frameNumber = m_frameCounter++;
    outFrame.captureTime = startTime;
    outFrame.isValid = true;

    return true;
}

void DXGICapture::ReleaseFrame() {
    if (m_frameAcquired && m_duplication) {
        m_duplication->ReleaseFrame();
        m_frameAcquired = false;
    }
}

void DXGICapture::ResetStats() {
    m_stats = CaptureStats{};
}

void DXGICapture::SetError(const std::string& error) {
    m_lastError = error;
}

} // namespace osfg
