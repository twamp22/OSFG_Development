// OSFG - Open Source Frame Generation
// Statistics Overlay
//
// Displays real-time performance statistics on screen.
// Uses Direct2D for efficient text rendering.
// MIT License - Part of Open Source Frame Generation project

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <cstdint>
#include <chrono>
#include <deque>

namespace osfg {

using Microsoft::WRL::ComPtr;

// Overlay position
enum class OverlayPosition {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

// Performance metrics to display
struct PerformanceMetrics {
    // Frame rates
    double baseFPS = 0.0;           // Original game FPS
    double outputFPS = 0.0;         // Output FPS after frame gen
    double targetFPS = 0.0;         // Target display refresh rate

    // Frame times
    double baseFrameTimeMs = 0.0;   // Base frame time
    double genFrameTimeMs = 0.0;    // Frame generation time
    double totalLatencyMs = 0.0;    // Total pipeline latency

    // Component timings
    double captureTimeMs = 0.0;
    double transferTimeMs = 0.0;
    double opticalFlowTimeMs = 0.0;
    double interpolationTimeMs = 0.0;
    double presentTimeMs = 0.0;

    // Frame counts
    uint64_t baseFrames = 0;
    uint64_t generatedFrames = 0;
    uint64_t droppedFrames = 0;

    // GPU usage (0-100%)
    float primaryGPUUsage = 0.0f;
    float secondaryGPUUsage = 0.0f;

    // Memory usage
    uint64_t vramUsageMB = 0;

    // Mode info
    bool frameGenEnabled = true;
    int frameGenMultiplier = 2;
    bool dualGPUMode = false;
};

// Overlay configuration
struct OverlayConfig {
    OverlayPosition position = OverlayPosition::TopLeft;
    float scale = 1.0f;
    float opacity = 0.8f;
    uint32_t backgroundColor = 0x80000000;  // ARGB
    uint32_t textColor = 0xFFFFFFFF;        // ARGB
    uint32_t accentColor = 0xFF00FF00;      // ARGB (green for good, red for bad)
    float padding = 10.0f;
    float lineSpacing = 4.0f;
    bool showFPS = true;
    bool showFrameTime = true;
    bool showComponentTimings = false;
    bool showGPUUsage = false;
    bool showMemory = false;
    bool showFrameCounts = false;
    bool compactMode = false;
};

// Statistics overlay renderer
class StatsOverlay {
public:
    StatsOverlay();
    ~StatsOverlay();

    // Disable copy
    StatsOverlay(const StatsOverlay&) = delete;
    StatsOverlay& operator=(const StatsOverlay&) = delete;

    // Initialize with a D3D11 device and swap chain
    bool Initialize(ID3D11Device* device, IDXGISwapChain* swapChain,
                   uint32_t width, uint32_t height);

    // Shutdown and release resources
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Set overlay visibility
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }

    // Toggle visibility
    void ToggleVisibility() { m_visible = !m_visible; }

    // Update metrics
    void UpdateMetrics(const PerformanceMetrics& metrics);

    // Render the overlay
    // Call this after your main rendering, before Present()
    void Render();

    // Handle resize
    void OnResize(uint32_t width, uint32_t height);

    // Configuration
    void SetConfig(const OverlayConfig& config);
    const OverlayConfig& GetConfig() const { return m_config; }

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

private:
    bool CreateD2DResources();
    bool CreateTextFormats();
    void CalculateLayout();
    void RenderBackground();
    void RenderText();

    std::wstring FormatFPS(double fps) const;
    std::wstring FormatFrameTime(double ms) const;
    std::wstring FormatPercentage(float pct) const;
    std::wstring FormatMemory(uint64_t mb) const;

    D2D1_COLOR_F ArgbToColorF(uint32_t argb) const;

    // D2D/DWrite resources
    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<ID2D1RenderTarget> m_renderTarget;
    ComPtr<ID2D1SolidColorBrush> m_backgroundBrush;
    ComPtr<ID2D1SolidColorBrush> m_textBrush;
    ComPtr<ID2D1SolidColorBrush> m_accentBrush;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IDWriteTextFormat> m_titleFormat;
    ComPtr<IDWriteTextFormat> m_valueFormat;

    // DXGI surface
    ComPtr<IDXGISurface> m_surface;

    // Configuration and state
    OverlayConfig m_config;
    PerformanceMetrics m_metrics;
    bool m_initialized = false;
    bool m_visible = true;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    std::string m_lastError;

    // Layout
    D2D1_RECT_F m_overlayRect = {};
    float m_fontSize = 14.0f;
    float m_lineHeight = 18.0f;

    // FPS smoothing
    std::deque<double> m_fpsHistory;
    static const size_t FPS_HISTORY_SIZE = 60;
};

} // namespace osfg
