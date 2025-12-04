// OSFG - Open Source Frame Generation
// Statistics Overlay Implementation

#include "stats_overlay.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace osfg {

StatsOverlay::StatsOverlay() = default;

StatsOverlay::~StatsOverlay() {
    Shutdown();
}

bool StatsOverlay::Initialize(ID3D11Device* device, IDXGISwapChain* swapChain,
                               uint32_t width, uint32_t height) {
    if (m_initialized) {
        Shutdown();
    }

    if (!device || !swapChain) {
        m_lastError = "Device or swap chain is null";
        return false;
    }

    m_width = width;
    m_height = height;

    // Get DXGI surface from swap chain
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&m_surface));
    if (FAILED(hr)) {
        m_lastError = "Failed to get swap chain surface";
        return false;
    }

    if (!CreateD2DResources()) {
        Shutdown();
        return false;
    }

    if (!CreateTextFormats()) {
        Shutdown();
        return false;
    }

    CalculateLayout();

    m_initialized = true;
    return true;
}

void StatsOverlay::Shutdown() {
    m_textFormat.Reset();
    m_titleFormat.Reset();
    m_valueFormat.Reset();
    m_accentBrush.Reset();
    m_textBrush.Reset();
    m_backgroundBrush.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    m_surface.Reset();

    m_fpsHistory.clear();
    m_initialized = false;
}

bool StatsOverlay::CreateD2DResources() {
    HRESULT hr;

    // Create D2D factory
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&m_d2dFactory));
    if (FAILED(hr)) {
        m_lastError = "Failed to create D2D factory";
        return false;
    }

    // Create render target from DXGI surface
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

    hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(m_surface.Get(), &props, &m_renderTarget);
    if (FAILED(hr)) {
        m_lastError = "Failed to create D2D render target";
        return false;
    }

    // Create brushes
    hr = m_renderTarget->CreateSolidColorBrush(
        ArgbToColorF(m_config.backgroundColor), &m_backgroundBrush);
    if (FAILED(hr)) {
        m_lastError = "Failed to create background brush";
        return false;
    }

    hr = m_renderTarget->CreateSolidColorBrush(
        ArgbToColorF(m_config.textColor), &m_textBrush);
    if (FAILED(hr)) {
        m_lastError = "Failed to create text brush";
        return false;
    }

    hr = m_renderTarget->CreateSolidColorBrush(
        ArgbToColorF(m_config.accentColor), &m_accentBrush);
    if (FAILED(hr)) {
        m_lastError = "Failed to create accent brush";
        return false;
    }

    // Create DWrite factory
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) {
        m_lastError = "Failed to create DWrite factory";
        return false;
    }

    return true;
}

bool StatsOverlay::CreateTextFormats() {
    HRESULT hr;

    m_fontSize = 14.0f * m_config.scale;
    m_lineHeight = m_fontSize + m_config.lineSpacing;

    // Main text format
    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_fontSize,
        L"en-US",
        &m_textFormat);
    if (FAILED(hr)) {
        m_lastError = "Failed to create text format";
        return false;
    }

    // Title format (slightly larger)
    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_fontSize * 1.1f,
        L"en-US",
        &m_titleFormat);
    if (FAILED(hr)) {
        m_lastError = "Failed to create title format";
        return false;
    }

    // Value format (monospace for alignment)
    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_fontSize,
        L"en-US",
        &m_valueFormat);
    if (FAILED(hr)) {
        m_lastError = "Failed to create value format";
        return false;
    }

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_valueFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    return true;
}

void StatsOverlay::CalculateLayout() {
    float overlayWidth = 200.0f * m_config.scale;
    float overlayHeight = 100.0f * m_config.scale;

    // Calculate number of lines
    int lineCount = 1; // Title
    if (m_config.showFPS) lineCount += 2;
    if (m_config.showFrameTime) lineCount += 2;
    if (m_config.showComponentTimings) lineCount += 5;
    if (m_config.showGPUUsage) lineCount += 2;
    if (m_config.showMemory) lineCount += 1;
    if (m_config.showFrameCounts) lineCount += 2;

    overlayHeight = m_config.padding * 2 + lineCount * m_lineHeight;

    // Calculate position
    float x = m_config.padding;
    float y = m_config.padding;

    switch (m_config.position) {
        case OverlayPosition::TopLeft:
            x = m_config.padding;
            y = m_config.padding;
            break;
        case OverlayPosition::TopRight:
            x = m_width - overlayWidth - m_config.padding;
            y = m_config.padding;
            break;
        case OverlayPosition::BottomLeft:
            x = m_config.padding;
            y = m_height - overlayHeight - m_config.padding;
            break;
        case OverlayPosition::BottomRight:
            x = m_width - overlayWidth - m_config.padding;
            y = m_height - overlayHeight - m_config.padding;
            break;
    }

    m_overlayRect = D2D1::RectF(x, y, x + overlayWidth, y + overlayHeight);
}

void StatsOverlay::UpdateMetrics(const PerformanceMetrics& metrics) {
    m_metrics = metrics;

    // Smooth FPS values
    m_fpsHistory.push_back(metrics.outputFPS);
    while (m_fpsHistory.size() > FPS_HISTORY_SIZE) {
        m_fpsHistory.pop_front();
    }
}

void StatsOverlay::Render() {
    if (!m_initialized || !m_visible) {
        return;
    }

    m_renderTarget->BeginDraw();

    RenderBackground();
    RenderText();

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        // Device lost - need to recreate resources
        m_initialized = false;
    }
}

void StatsOverlay::RenderBackground() {
    // Draw rounded rectangle background
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(m_overlayRect, 8.0f, 8.0f);
    m_renderTarget->FillRoundedRectangle(roundedRect, m_backgroundBrush.Get());
}

void StatsOverlay::RenderText() {
    float x = m_overlayRect.left + m_config.padding;
    float y = m_overlayRect.top + m_config.padding;
    float width = m_overlayRect.right - m_overlayRect.left - m_config.padding * 2;

    // Title
    std::wstring title = L"OSFG";
    if (m_metrics.frameGenEnabled) {
        title += L" [" + std::to_wstring(m_metrics.frameGenMultiplier) + L"X]";
    } else {
        title += L" [OFF]";
    }
    if (m_metrics.dualGPUMode) {
        title += L" Dual";
    }

    D2D1_RECT_F textRect = D2D1::RectF(x, y, x + width, y + m_lineHeight);
    m_renderTarget->DrawText(title.c_str(), static_cast<UINT32>(title.length()),
        m_titleFormat.Get(), textRect, m_accentBrush.Get());
    y += m_lineHeight + 4;

    // FPS
    if (m_config.showFPS) {
        // Calculate smoothed FPS
        double smoothedFPS = 0.0;
        if (!m_fpsHistory.empty()) {
            for (double fps : m_fpsHistory) {
                smoothedFPS += fps;
            }
            smoothedFPS /= m_fpsHistory.size();
        }

        std::wstring label = L"FPS:";
        std::wstring value = FormatFPS(smoothedFPS);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, m_accentBrush.Get());
        y += m_lineHeight;

        // Base FPS
        label = L"Base:";
        value = FormatFPS(m_metrics.baseFPS);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, m_textBrush.Get());
        y += m_lineHeight;
    }

    // Frame times
    if (m_config.showFrameTime) {
        std::wstring label = L"Frame:";
        std::wstring value = FormatFrameTime(m_metrics.baseFrameTimeMs);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, m_textBrush.Get());
        y += m_lineHeight;

        // Generation time
        label = L"Gen:";
        value = FormatFrameTime(m_metrics.genFrameTimeMs);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, m_textBrush.Get());
        y += m_lineHeight;
    }

    // Component timings
    if (m_config.showComponentTimings) {
        struct TimingEntry {
            const wchar_t* label;
            double value;
        };

        TimingEntry entries[] = {
            { L"Capture:", m_metrics.captureTimeMs },
            { L"Transfer:", m_metrics.transferTimeMs },
            { L"OptFlow:", m_metrics.opticalFlowTimeMs },
            { L"Interp:", m_metrics.interpolationTimeMs },
            { L"Present:", m_metrics.presentTimeMs }
        };

        for (const auto& entry : entries) {
            std::wstring label = entry.label;
            std::wstring value = FormatFrameTime(entry.value);

            textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
            m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
                m_textFormat.Get(), textRect, m_textBrush.Get());

            textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
            m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
                m_valueFormat.Get(), textRect, m_textBrush.Get());
            y += m_lineHeight;
        }
    }

    // GPU usage
    if (m_config.showGPUUsage) {
        std::wstring label = L"GPU1:";
        std::wstring value = FormatPercentage(m_metrics.primaryGPUUsage);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, m_textBrush.Get());
        y += m_lineHeight;

        if (m_metrics.dualGPUMode) {
            label = L"GPU2:";
            value = FormatPercentage(m_metrics.secondaryGPUUsage);

            textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
            m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
                m_textFormat.Get(), textRect, m_textBrush.Get());

            textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
            m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
                m_valueFormat.Get(), textRect, m_textBrush.Get());
            y += m_lineHeight;
        }
    }

    // Memory
    if (m_config.showMemory) {
        std::wstring label = L"VRAM:";
        std::wstring value = FormatMemory(m_metrics.vramUsageMB);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, m_textBrush.Get());
        y += m_lineHeight;
    }

    // Frame counts
    if (m_config.showFrameCounts) {
        std::wstring label = L"Gen:";
        std::wstring value = std::to_wstring(m_metrics.generatedFrames);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, m_textBrush.Get());
        y += m_lineHeight;

        label = L"Drop:";
        value = std::to_wstring(m_metrics.droppedFrames);

        textRect = D2D1::RectF(x, y, x + width * 0.5f, y + m_lineHeight);
        m_renderTarget->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
            m_textFormat.Get(), textRect, m_textBrush.Get());

        // Use red color if frames are being dropped
        ID2D1Brush* valueBrush = m_metrics.droppedFrames > 0 ?
            static_cast<ID2D1Brush*>(m_accentBrush.Get()) : m_textBrush.Get();

        textRect = D2D1::RectF(x + width * 0.5f, y, x + width, y + m_lineHeight);
        m_renderTarget->DrawText(value.c_str(), static_cast<UINT32>(value.length()),
            m_valueFormat.Get(), textRect, valueBrush);
    }
}

void StatsOverlay::OnResize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    CalculateLayout();
}

void StatsOverlay::SetConfig(const OverlayConfig& config) {
    m_config = config;

    // Update brushes with new colors
    if (m_renderTarget) {
        m_backgroundBrush->SetColor(ArgbToColorF(config.backgroundColor));
        m_textBrush->SetColor(ArgbToColorF(config.textColor));
        m_accentBrush->SetColor(ArgbToColorF(config.accentColor));
    }

    // Recalculate layout
    CalculateLayout();
}

std::wstring StatsOverlay::FormatFPS(double fps) const {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1) << fps;
    return ss.str();
}

std::wstring StatsOverlay::FormatFrameTime(double ms) const {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2) << ms << L" ms";
    return ss.str();
}

std::wstring StatsOverlay::FormatPercentage(float pct) const {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(0) << pct << L"%";
    return ss.str();
}

std::wstring StatsOverlay::FormatMemory(uint64_t mb) const {
    std::wstringstream ss;
    ss << mb << L" MB";
    return ss.str();
}

D2D1_COLOR_F StatsOverlay::ArgbToColorF(uint32_t argb) const {
    return D2D1::ColorF(
        ((argb >> 16) & 0xFF) / 255.0f,  // R
        ((argb >> 8) & 0xFF) / 255.0f,   // G
        (argb & 0xFF) / 255.0f,          // B
        ((argb >> 24) & 0xFF) / 255.0f   // A
    );
}

} // namespace osfg
