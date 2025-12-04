// OSFG - Open Source Frame Generation
// Configuration Manager Implementation

#include "config_manager.h"
#include <fstream>
#include <sstream>
#include <ShlObj.h>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "Shell32.lib")

namespace osfg {

// Simple JSON-like parser (minimal implementation without external dependencies)
namespace {

std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool ParseBool(const std::string& value) {
    std::string lower = ToLower(Trim(value));
    return lower == "true" || lower == "1" || lower == "yes";
}

uint32_t ParseUInt(const std::string& value) {
    try {
        return static_cast<uint32_t>(std::stoul(Trim(value)));
    } catch (...) {
        return 0;
    }
}

float ParseFloat(const std::string& value) {
    try {
        return std::stof(Trim(value));
    } catch (...) {
        return 0.0f;
    }
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

} // anonymous namespace

ConfigManager::ConfigManager() = default;

ConfigManager::~ConfigManager() = default;

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::Load(const std::wstring& path) {
    std::wstring configPath = path.empty() ? GetDefaultConfigPath() : path;
    m_configPath = configPath;

    if (!ParseConfigFile(configPath)) {
        // If file doesn't exist, create with defaults
        m_settings = AppSettings{};
        return Save(configPath);
    }

    return true;
}

bool ConfigManager::Save(const std::wstring& path) {
    std::wstring configPath = path.empty() ? m_configPath : path;
    if (configPath.empty()) {
        configPath = GetDefaultConfigPath();
    }

    return WriteConfigFile(configPath);
}

void ConfigManager::ResetToDefaults() {
    m_settings = AppSettings{};
    NotifyCallbacks();
}

void ConfigManager::ApplySettings() {
    if (ValidateSettings(m_settings)) {
        NotifyCallbacks();
    }
}

void ConfigManager::RegisterCallback(SettingsChangedCallback callback) {
    m_callbacks.push_back(callback);
}

std::wstring ConfigManager::GetDefaultConfigPath() const {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring path = appDataPath;
        path += L"\\OSFG";

        // Create directory if it doesn't exist
        CreateDirectoryW(path.c_str(), nullptr);

        path += L"\\config.ini";
        return path;
    }

    return L"osfg_config.ini";
}

bool ConfigManager::ValidateSettings(const AppSettings& settings) {
    // Validate GPU indices
    if (settings.gpuMode == GPUMode::DualGPU) {
        if (settings.primaryGPU == settings.secondaryGPU) {
            m_lastError = L"Primary and secondary GPU cannot be the same";
            return false;
        }
    }

    // Validate optical flow settings
    if (settings.opticalFlowBlockSize < 4 || settings.opticalFlowBlockSize > 32) {
        m_lastError = L"Optical flow block size must be between 4 and 32";
        return false;
    }

    if (settings.sceneChangeThreshold < 0.0f || settings.sceneChangeThreshold > 1.0f) {
        m_lastError = L"Scene change threshold must be between 0.0 and 1.0";
        return false;
    }

    return true;
}

bool ConfigManager::ParseConfigFile(const std::wstring& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        m_lastError = L"Failed to open config file";
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        line = Trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            currentSection = ToLower(line.substr(1, line.length() - 2));
            continue;
        }

        // Key-value pair
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = ToLower(Trim(line.substr(0, eqPos)));
        std::string value = Trim(line.substr(eqPos + 1));

        // Remove quotes from string values
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
        }

        // Parse based on section
        if (currentSection == "framegen") {
            if (key == "mode") m_settings.frameGenMode = StringToFrameGenMode(value);
            else if (key == "enabled") m_settings.enableFrameGen = ParseBool(value);
            else if (key == "targetframerate") m_settings.targetFramerate = ParseFloat(value);
        }
        else if (currentSection == "capture") {
            if (key == "method") m_settings.captureMethod = StringToCaptureMethod(value);
            else if (key == "monitor") m_settings.captureMonitor = ParseUInt(value);
            else if (key == "cursor") m_settings.captureCursor = ParseBool(value);
        }
        else if (currentSection == "gpu") {
            if (key == "mode") m_settings.gpuMode = StringToGPUMode(value);
            else if (key == "primary") m_settings.primaryGPU = ParseUInt(value);
            else if (key == "secondary") m_settings.secondaryGPU = ParseUInt(value);
        }
        else if (currentSection == "opticalflow") {
            if (key == "blocksize") m_settings.opticalFlowBlockSize = ParseUInt(value);
            else if (key == "searchradius") m_settings.opticalFlowSearchRadius = ParseUInt(value);
            else if (key == "scenechangethreshold") m_settings.sceneChangeThreshold = ParseFloat(value);
        }
        else if (currentSection == "presentation") {
            if (key == "vsync") m_settings.vsyncEnabled = ParseBool(value);
            else if (key == "borderless") m_settings.borderlessWindow = ParseBool(value);
            else if (key == "width") m_settings.windowWidth = ParseUInt(value);
            else if (key == "height") m_settings.windowHeight = ParseUInt(value);
        }
        else if (currentSection == "overlay") {
            if (key == "show") m_settings.showOverlay = ParseBool(value);
            else if (key == "fps") m_settings.showFPS = ParseBool(value);
            else if (key == "frametime") m_settings.showFrameTime = ParseBool(value);
            else if (key == "gpuusage") m_settings.showGPUUsage = ParseBool(value);
            else if (key == "position") m_settings.overlayPosition = ParseUInt(value);
            else if (key == "scale") m_settings.overlayScale = ParseFloat(value);
        }
        else if (currentSection == "hotkeys") {
            if (key == "toggleframegen") m_settings.hotkeyToggleFrameGen = ParseUInt(value);
            else if (key == "toggleoverlay") m_settings.hotkeyToggleOverlay = ParseUInt(value);
            else if (key == "cyclemode") m_settings.hotkeyCycleMode = ParseUInt(value);
            else if (key == "requirealt") m_settings.hotkeyRequireAlt = ParseBool(value);
        }
        else if (currentSection == "advanced") {
            if (key == "framebuffercount") m_settings.frameBufferCount = ParseUInt(value);
            else if (key == "peertopeer") m_settings.usePeerToPeerTransfer = ParseBool(value);
            else if (key == "debug") m_settings.enableDebugMode = ParseBool(value);
            else if (key == "logfile") m_settings.logFilePath = StringToWString(value);
        }
    }

    return true;
}

bool ConfigManager::WriteConfigFile(const std::wstring& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        m_lastError = L"Failed to create config file";
        return false;
    }

    file << "# OSFG Configuration File\n";
    file << "# Generated automatically - edit with care\n\n";

    file << "[FrameGen]\n";
    file << "Mode = " << FrameGenModeToString(m_settings.frameGenMode) << "\n";
    file << "Enabled = " << (m_settings.enableFrameGen ? "true" : "false") << "\n";
    file << "TargetFramerate = " << m_settings.targetFramerate << "\n\n";

    file << "[Capture]\n";
    file << "Method = " << CaptureMethodToString(m_settings.captureMethod) << "\n";
    file << "Monitor = " << m_settings.captureMonitor << "\n";
    file << "Cursor = " << (m_settings.captureCursor ? "true" : "false") << "\n\n";

    file << "[GPU]\n";
    file << "Mode = " << GPUModeToString(m_settings.gpuMode) << "\n";
    file << "Primary = " << m_settings.primaryGPU << "\n";
    file << "Secondary = " << m_settings.secondaryGPU << "\n\n";

    file << "[OpticalFlow]\n";
    file << "BlockSize = " << m_settings.opticalFlowBlockSize << "\n";
    file << "SearchRadius = " << m_settings.opticalFlowSearchRadius << "\n";
    file << "SceneChangeThreshold = " << m_settings.sceneChangeThreshold << "\n\n";

    file << "[Presentation]\n";
    file << "VSync = " << (m_settings.vsyncEnabled ? "true" : "false") << "\n";
    file << "Borderless = " << (m_settings.borderlessWindow ? "true" : "false") << "\n";
    file << "Width = " << m_settings.windowWidth << "\n";
    file << "Height = " << m_settings.windowHeight << "\n\n";

    file << "[Overlay]\n";
    file << "Show = " << (m_settings.showOverlay ? "true" : "false") << "\n";
    file << "FPS = " << (m_settings.showFPS ? "true" : "false") << "\n";
    file << "FrameTime = " << (m_settings.showFrameTime ? "true" : "false") << "\n";
    file << "GPUUsage = " << (m_settings.showGPUUsage ? "true" : "false") << "\n";
    file << "Position = " << m_settings.overlayPosition << "\n";
    file << "Scale = " << m_settings.overlayScale << "\n\n";

    file << "[Hotkeys]\n";
    file << "ToggleFrameGen = " << m_settings.hotkeyToggleFrameGen << "\n";
    file << "ToggleOverlay = " << m_settings.hotkeyToggleOverlay << "\n";
    file << "CycleMode = " << m_settings.hotkeyCycleMode << "\n";
    file << "RequireAlt = " << (m_settings.hotkeyRequireAlt ? "true" : "false") << "\n\n";

    file << "[Advanced]\n";
    file << "FrameBufferCount = " << m_settings.frameBufferCount << "\n";
    file << "PeerToPeer = " << (m_settings.usePeerToPeerTransfer ? "true" : "false") << "\n";
    file << "Debug = " << (m_settings.enableDebugMode ? "true" : "false") << "\n";
    file << "LogFile = \"" << WStringToString(m_settings.logFilePath) << "\"\n";

    return true;
}

void ConfigManager::NotifyCallbacks() {
    for (auto& callback : m_callbacks) {
        callback(m_settings);
    }
}

const char* ConfigManager::FrameGenModeToString(FrameGenMode mode) {
    switch (mode) {
        case FrameGenMode::Disabled: return "Disabled";
        case FrameGenMode::FrameGen2X: return "2X";
        case FrameGenMode::FrameGen3X: return "3X";
        case FrameGenMode::FrameGen4X: return "4X";
        default: return "2X";
    }
}

const char* ConfigManager::CaptureMethodToString(CaptureMethod method) {
    switch (method) {
        case CaptureMethod::Auto: return "Auto";
        case CaptureMethod::DXGIDesktopDup: return "DXGI";
        case CaptureMethod::WindowsGraphicsCapture: return "WGC";
        default: return "Auto";
    }
}

const char* ConfigManager::GPUModeToString(GPUMode mode) {
    switch (mode) {
        case GPUMode::SingleGPU: return "Single";
        case GPUMode::DualGPU: return "Dual";
        case GPUMode::Auto: return "Auto";
        default: return "Auto";
    }
}

FrameGenMode ConfigManager::StringToFrameGenMode(const std::string& str) {
    std::string lower = ToLower(Trim(str));
    if (lower == "disabled" || lower == "off" || lower == "0") return FrameGenMode::Disabled;
    if (lower == "2x" || lower == "2") return FrameGenMode::FrameGen2X;
    if (lower == "3x" || lower == "3") return FrameGenMode::FrameGen3X;
    if (lower == "4x" || lower == "4") return FrameGenMode::FrameGen4X;
    return FrameGenMode::FrameGen2X;
}

CaptureMethod ConfigManager::StringToCaptureMethod(const std::string& str) {
    std::string lower = ToLower(Trim(str));
    if (lower == "auto") return CaptureMethod::Auto;
    if (lower == "dxgi" || lower == "desktopdup") return CaptureMethod::DXGIDesktopDup;
    if (lower == "wgc" || lower == "windowsgraphicscapture") return CaptureMethod::WindowsGraphicsCapture;
    return CaptureMethod::Auto;
}

GPUMode ConfigManager::StringToGPUMode(const std::string& str) {
    std::string lower = ToLower(Trim(str));
    if (lower == "single" || lower == "singlegpu") return GPUMode::SingleGPU;
    if (lower == "dual" || lower == "dualgpu") return GPUMode::DualGPU;
    if (lower == "auto") return GPUMode::Auto;
    return GPUMode::Auto;
}

} // namespace osfg
