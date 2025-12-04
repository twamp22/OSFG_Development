# Application Module API

The application module (`osfg_app`) provides configuration management, hotkey handling, and statistics overlay functionality.

## Headers

```cpp
#include "app/config_manager.h"
#include "app/hotkey_handler.h"
#include "app/stats_overlay.h"
```

## Namespace

```cpp
namespace osfg { ... }
```

---

## ConfigManager

Singleton class for managing application configuration.

### Access

```cpp
// Get singleton instance
static ConfigManager& Instance();
```

### File Operations

```cpp
// Load settings from file (empty = default path)
bool Load(const std::wstring& path = L"");

// Save settings to file
bool Save(const std::wstring& path = L"");

// Reset to defaults
void ResetToDefaults();

// Get default config file path
std::wstring GetDefaultConfigPath() const;
```

### Settings Access

```cpp
// Get current settings (read-only)
const AppSettings& GetSettings() const;

// Get mutable settings for modification
AppSettings& GetSettingsMutable();

// Apply modified settings (triggers callbacks)
void ApplySettings();

// Validate settings
bool ValidateSettings(const AppSettings& settings);
```

### Callbacks

```cpp
// Register callback for settings changes
void RegisterCallback(SettingsChangedCallback callback);

// Callback type
using SettingsChangedCallback = std::function<void(const AppSettings&)>;
```

### AppSettings Structure

```cpp
struct AppSettings {
    // Frame generation
    FrameGenMode frameGenMode;
    bool enableFrameGen;
    float targetFramerate;

    // Capture
    CaptureMethod captureMethod;
    uint32_t captureMonitor;
    bool captureCursor;

    // GPU
    GPUMode gpuMode;
    uint32_t primaryGPU;
    uint32_t secondaryGPU;

    // Optical flow
    uint32_t opticalFlowBlockSize;
    uint32_t opticalFlowSearchRadius;
    float sceneChangeThreshold;

    // Presentation
    bool vsyncEnabled;
    bool borderlessWindow;
    uint32_t windowWidth;
    uint32_t windowHeight;

    // Overlay
    bool showOverlay;
    bool showFPS;
    bool showFrameTime;
    bool showGPUUsage;
    uint32_t overlayPosition;
    float overlayScale;

    // Hotkeys
    uint32_t hotkeyToggleFrameGen;
    uint32_t hotkeyToggleOverlay;
    uint32_t hotkeyCycleMode;
    bool hotkeyRequireAlt;

    // Advanced
    uint32_t frameBufferCount;
    bool usePeerToPeerTransfer;
    bool enableDebugMode;
    std::wstring logFilePath;
};
```

### Usage Example

```cpp
auto& config = osfg::ConfigManager::Instance();

// Load configuration
config.Load();

// Register for changes
config.RegisterCallback([](const AppSettings& settings) {
    printf("Settings changed! Frame gen: %s\n",
           settings.enableFrameGen ? "ON" : "OFF");
});

// Modify settings
auto& settings = config.GetSettingsMutable();
settings.frameGenMode = FrameGenMode::FrameGen3X;
config.ApplySettings();

// Save
config.Save();
```

---

## HotkeyHandler

Handles global keyboard shortcuts.

### Initialization

```cpp
HotkeyHandler();
~HotkeyHandler();

bool Initialize();
void Shutdown();
bool IsInitialized() const;
```

### Hotkey Registration

```cpp
// Register a hotkey binding
bool RegisterHotkey(const HotkeyBinding& binding);

// Unregister a hotkey
bool UnregisterHotkey(HotkeyAction action);

// Unregister all hotkeys
void UnregisterAllHotkeys();

// Register default hotkeys from config
void RegisterDefaultHotkeys(uint32_t toggleFrameGen, uint32_t toggleOverlay,
                            uint32_t cycleMode, bool requireAlt);
```

### Event Handling

```cpp
// Set callback for hotkey events
void SetCallback(HotkeyCallback callback);

// Process Windows messages (call from message loop)
bool ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

// Callback type
using HotkeyCallback = std::function<void(HotkeyAction action)>;
```

### HotkeyBinding Structure

```cpp
struct HotkeyBinding {
    uint32_t virtualKey;           // Virtual key code (VK_F10, etc.)
    ModifierKey modifiers;         // Alt, Ctrl, Shift, Win
    HotkeyAction action;           // Action to trigger
    bool enabled;                  // Enable/disable binding

    std::string ToString() const;  // Get display string
};
```

### Enumerations

```cpp
enum class HotkeyAction {
    ToggleFrameGen,
    ToggleOverlay,
    CycleMode,
    IncreaseMultiplier,
    DecreaseMultiplier,
    ResetStats,
    TakeScreenshot,
    Custom
};

enum class ModifierKey : uint32_t {
    None  = 0,
    Alt   = 1 << 0,
    Ctrl  = 1 << 1,
    Shift = 1 << 2,
    Win   = 1 << 3
};
```

### Usage Example

```cpp
osfg::HotkeyHandler hotkeys;

if (!hotkeys.Initialize()) {
    printf("Failed to initialize hotkeys\n");
    return;
}

// Set callback
hotkeys.SetCallback([](HotkeyAction action) {
    switch (action) {
        case HotkeyAction::ToggleFrameGen:
            ToggleFrameGeneration();
            break;
        case HotkeyAction::ToggleOverlay:
            ToggleOverlay();
            break;
        case HotkeyAction::CycleMode:
            CycleFrameGenMode();
            break;
    }
});

// Register default hotkeys
hotkeys.RegisterDefaultHotkeys(VK_F10, VK_F11, VK_F12, true);

// Or register custom binding
HotkeyBinding binding;
binding.virtualKey = 'G';
binding.modifiers = ModifierKey::Ctrl | ModifierKey::Shift;
binding.action = HotkeyAction::ToggleFrameGen;
hotkeys.RegisterHotkey(binding);

// Message loop
MSG msg;
while (GetMessage(&msg, nullptr, 0, 0)) {
    hotkeys.ProcessMessage(msg.message, msg.wParam, msg.lParam);
    TranslateMessage(&msg);
    DispatchMessage(&msg);
}

hotkeys.Shutdown();
```

---

## StatsOverlay

Renders performance statistics using Direct2D.

### Initialization

```cpp
StatsOverlay();
~StatsOverlay();

// Initialize with D3D11 device and swap chain
bool Initialize(ID3D11Device* device, IDXGISwapChain* swapChain,
                uint32_t width, uint32_t height);

void Shutdown();
bool IsInitialized() const;
```

### Visibility

```cpp
void SetVisible(bool visible);
bool IsVisible() const;
void ToggleVisibility();
```

### Rendering

```cpp
// Update metrics before rendering
void UpdateMetrics(const PerformanceMetrics& metrics);

// Render the overlay (call before Present)
void Render();

// Handle window resize
void OnResize(uint32_t width, uint32_t height);
```

### Configuration

```cpp
void SetConfig(const OverlayConfig& config);
const OverlayConfig& GetConfig() const;
```

### PerformanceMetrics Structure

```cpp
struct PerformanceMetrics {
    // Frame rates
    double baseFPS;
    double outputFPS;
    double targetFPS;

    // Frame times
    double baseFrameTimeMs;
    double genFrameTimeMs;
    double totalLatencyMs;

    // Component timings
    double captureTimeMs;
    double transferTimeMs;
    double opticalFlowTimeMs;
    double interpolationTimeMs;
    double presentTimeMs;

    // Frame counts
    uint64_t baseFrames;
    uint64_t generatedFrames;
    uint64_t droppedFrames;

    // GPU usage
    float primaryGPUUsage;
    float secondaryGPUUsage;
    uint64_t vramUsageMB;

    // Mode info
    bool frameGenEnabled;
    int frameGenMultiplier;
    bool dualGPUMode;
};
```

### OverlayConfig Structure

```cpp
struct OverlayConfig {
    OverlayPosition position;      // Corner position
    float scale;                   // Scale factor
    float opacity;                 // Background opacity
    uint32_t backgroundColor;      // ARGB color
    uint32_t textColor;            // ARGB color
    uint32_t accentColor;          // ARGB color
    float padding;                 // Edge padding
    float lineSpacing;             // Line spacing
    bool showFPS;
    bool showFrameTime;
    bool showComponentTimings;
    bool showGPUUsage;
    bool showMemory;
    bool showFrameCounts;
    bool compactMode;
};
```

### Usage Example

```cpp
osfg::StatsOverlay overlay;

// Initialize with D3D11 resources
if (!overlay.Initialize(d3d11Device, swapChain, 1920, 1080)) {
    printf("Failed to initialize overlay\n");
    return;
}

// Configure appearance
OverlayConfig config;
config.position = OverlayPosition::TopLeft;
config.showFPS = true;
config.showFrameTime = true;
config.scale = 1.0f;
overlay.SetConfig(config);

// Render loop
while (running) {
    // Update metrics
    PerformanceMetrics metrics;
    metrics.outputFPS = currentFPS;
    metrics.baseFrameTimeMs = frameTime;
    metrics.frameGenEnabled = true;
    metrics.frameGenMultiplier = 2;
    overlay.UpdateMetrics(metrics);

    // Render scene...

    // Render overlay (before Present)
    if (overlay.IsVisible()) {
        overlay.Render();
    }

    swapChain->Present(1, 0);
}

overlay.Shutdown();
```

---

## Integration Example

Complete example integrating all application components:

```cpp
#include "app/config_manager.h"
#include "app/hotkey_handler.h"
#include "app/stats_overlay.h"

class Application {
    osfg::HotkeyHandler m_hotkeys;
    osfg::StatsOverlay m_overlay;
    bool m_frameGenEnabled = true;

public:
    bool Initialize(ID3D11Device* device, IDXGISwapChain* swapChain) {
        // Load configuration
        auto& config = osfg::ConfigManager::Instance();
        config.Load();

        const auto& settings = config.GetSettings();

        // Initialize hotkeys
        if (!m_hotkeys.Initialize()) return false;

        m_hotkeys.SetCallback([this](osfg::HotkeyAction action) {
            OnHotkey(action);
        });

        m_hotkeys.RegisterDefaultHotkeys(
            settings.hotkeyToggleFrameGen,
            settings.hotkeyToggleOverlay,
            settings.hotkeyCycleMode,
            settings.hotkeyRequireAlt
        );

        // Initialize overlay
        if (!m_overlay.Initialize(device, swapChain,
                                   settings.windowWidth,
                                   settings.windowHeight)) {
            return false;
        }

        m_overlay.SetVisible(settings.showOverlay);

        return true;
    }

    void OnHotkey(osfg::HotkeyAction action) {
        switch (action) {
            case osfg::HotkeyAction::ToggleFrameGen:
                m_frameGenEnabled = !m_frameGenEnabled;
                break;
            case osfg::HotkeyAction::ToggleOverlay:
                m_overlay.ToggleVisibility();
                break;
        }
    }

    void Shutdown() {
        m_overlay.Shutdown();
        m_hotkeys.Shutdown();
    }
};
```
