// OSFG - Open Source Frame Generation
// Configuration Manager
//
// Handles loading, saving, and validating application settings.
// Settings are stored in a JSON configuration file.
// MIT License - Part of Open Source Frame Generation project

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <string>
#include <cstdint>
#include <functional>

namespace osfg {

// Frame generation mode
enum class FrameGenMode {
    Disabled,       // No frame generation
    FrameGen2X,     // Double framerate (60->120)
    FrameGen3X,     // Triple framerate (60->180)
    FrameGen4X      // Quadruple framerate (60->240)
};

// Capture method preference
enum class CaptureMethod {
    Auto,           // Automatically select best method
    DXGIDesktopDup, // DXGI Desktop Duplication
    WindowsGraphicsCapture  // Windows.Graphics.Capture API
};

// GPU selection mode
enum class GPUMode {
    SingleGPU,      // Use primary GPU for everything
    DualGPU,        // Use secondary GPU for frame generation
    Auto            // Automatically detect best configuration
};

// Application settings
struct AppSettings {
    // Frame generation settings
    FrameGenMode frameGenMode = FrameGenMode::FrameGen2X;
    bool enableFrameGen = true;
    float targetFramerate = 0.0f;  // 0 = match display refresh rate

    // Capture settings
    CaptureMethod captureMethod = CaptureMethod::Auto;
    uint32_t captureMonitor = 0;
    bool captureCursor = true;

    // GPU settings
    GPUMode gpuMode = GPUMode::Auto;
    uint32_t primaryGPU = 0;
    uint32_t secondaryGPU = 1;

    // Optical flow settings
    uint32_t opticalFlowBlockSize = 8;
    uint32_t opticalFlowSearchRadius = 12;
    float sceneChangeThreshold = 0.5f;

    // Presentation settings
    bool vsyncEnabled = true;
    bool borderlessWindow = true;
    uint32_t windowWidth = 1920;
    uint32_t windowHeight = 1080;

    // Overlay settings
    bool showOverlay = true;
    bool showFPS = true;
    bool showFrameTime = true;
    bool showGPUUsage = false;
    uint32_t overlayPosition = 0;  // 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight
    float overlayScale = 1.0f;

    // Hotkey settings (virtual key codes)
    uint32_t hotkeyToggleFrameGen = VK_F10;
    uint32_t hotkeyToggleOverlay = VK_F11;
    uint32_t hotkeyCycleMode = VK_F12;
    bool hotkeyRequireAlt = true;

    // Advanced settings
    uint32_t frameBufferCount = 3;
    bool usePeerToPeerTransfer = true;
    bool enableDebugMode = false;
    std::wstring logFilePath = L"";
};

// Callback for settings changes
using SettingsChangedCallback = std::function<void(const AppSettings&)>;

// Configuration manager
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    // Disable copy
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // Get singleton instance
    static ConfigManager& Instance();

    // Load settings from file
    // Uses default config path if path is empty
    bool Load(const std::wstring& path = L"");

    // Save settings to file
    bool Save(const std::wstring& path = L"");

    // Reset to defaults
    void ResetToDefaults();

    // Get current settings (const reference)
    const AppSettings& GetSettings() const { return m_settings; }

    // Get mutable settings for modification
    AppSettings& GetSettingsMutable() { return m_settings; }

    // Apply modified settings
    void ApplySettings();

    // Register callback for settings changes
    void RegisterCallback(SettingsChangedCallback callback);

    // Get default config file path
    std::wstring GetDefaultConfigPath() const;

    // Get last error
    const std::wstring& GetLastError() const { return m_lastError; }

    // Validate settings (returns true if valid)
    bool ValidateSettings(const AppSettings& settings);

    // Get string representation of enum values
    static const char* FrameGenModeToString(FrameGenMode mode);
    static const char* CaptureMethodToString(CaptureMethod method);
    static const char* GPUModeToString(GPUMode mode);

    // Parse enum from string
    static FrameGenMode StringToFrameGenMode(const std::string& str);
    static CaptureMethod StringToCaptureMethod(const std::string& str);
    static GPUMode StringToGPUMode(const std::string& str);

private:
    bool ParseConfigFile(const std::wstring& path);
    bool WriteConfigFile(const std::wstring& path);
    void NotifyCallbacks();

    AppSettings m_settings;
    std::wstring m_configPath;
    std::wstring m_lastError;
    std::vector<SettingsChangedCallback> m_callbacks;
};

} // namespace osfg
