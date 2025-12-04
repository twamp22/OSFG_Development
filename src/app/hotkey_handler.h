// OSFG - Open Source Frame Generation
// Hotkey Handler
//
// Handles global keyboard shortcuts for controlling frame generation.
// Supports customizable key combinations with modifier keys.
// MIT License - Part of Open Source Frame Generation project

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <functional>
#include <map>
#include <string>
#include <cstdint>
#include <atomic>
#include <thread>

namespace osfg {

// Hotkey action types
enum class HotkeyAction {
    ToggleFrameGen,     // Enable/disable frame generation
    ToggleOverlay,      // Show/hide statistics overlay
    CycleMode,          // Cycle through frame gen modes (2X -> 3X -> 4X -> 2X)
    IncreaseMultiplier, // Increase frame generation multiplier
    DecreaseMultiplier, // Decrease frame generation multiplier
    ResetStats,         // Reset performance statistics
    TakeScreenshot,     // Capture screenshot
    Custom              // Custom user-defined action
};

// Modifier key flags
enum class ModifierKey : uint32_t {
    None  = 0,
    Alt   = 1 << 0,
    Ctrl  = 1 << 1,
    Shift = 1 << 2,
    Win   = 1 << 3
};

// Enable bitwise operations on ModifierKey
inline ModifierKey operator|(ModifierKey a, ModifierKey b) {
    return static_cast<ModifierKey>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ModifierKey operator&(ModifierKey a, ModifierKey b) {
    return static_cast<ModifierKey>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasModifier(ModifierKey flags, ModifierKey modifier) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(modifier)) != 0;
}

// Hotkey binding
struct HotkeyBinding {
    uint32_t virtualKey = 0;        // Virtual key code (VK_F10, etc.)
    ModifierKey modifiers = ModifierKey::None;
    HotkeyAction action = HotkeyAction::ToggleFrameGen;
    bool enabled = true;

    // Generate unique ID for this binding
    int GetId() const {
        return static_cast<int>(virtualKey) |
               (static_cast<int>(modifiers) << 16);
    }

    // Get display string for this hotkey
    std::string ToString() const;
};

// Callback for hotkey events
using HotkeyCallback = std::function<void(HotkeyAction action)>;

// Hotkey handler
class HotkeyHandler {
public:
    HotkeyHandler();
    ~HotkeyHandler();

    // Disable copy
    HotkeyHandler(const HotkeyHandler&) = delete;
    HotkeyHandler& operator=(const HotkeyHandler&) = delete;

    // Initialize hotkey handler
    bool Initialize();

    // Shutdown and unregister all hotkeys
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Register a hotkey binding
    bool RegisterHotkey(const HotkeyBinding& binding);

    // Unregister a hotkey
    bool UnregisterHotkey(HotkeyAction action);

    // Unregister all hotkeys
    void UnregisterAllHotkeys();

    // Set callback for hotkey events
    void SetCallback(HotkeyCallback callback);

    // Process Windows messages (call from message loop)
    // Returns true if a hotkey message was processed
    bool ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Register default hotkeys based on config
    void RegisterDefaultHotkeys(uint32_t toggleFrameGen, uint32_t toggleOverlay,
                                 uint32_t cycleMode, bool requireAlt);

    // Get binding for action (if registered)
    const HotkeyBinding* GetBinding(HotkeyAction action) const;

    // Convert virtual key code to string
    static std::string VirtualKeyToString(uint32_t vk);

    // Parse virtual key from string
    static uint32_t StringToVirtualKey(const std::string& str);

    // Get last error
    const std::string& GetLastError() const { return m_lastError; }

private:
    bool RegisterSystemHotkey(const HotkeyBinding& binding);
    void UnregisterSystemHotkey(int id);
    uint32_t ModifiersToWin32(ModifierKey modifiers) const;

    std::map<int, HotkeyBinding> m_bindings;       // ID -> Binding
    std::map<HotkeyAction, int> m_actionToId;      // Action -> ID
    HotkeyCallback m_callback;
    bool m_initialized = false;
    std::string m_lastError;
    HWND m_messageWindow = nullptr;

    // Hidden message window for receiving hotkey messages
    static LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static HotkeyHandler* s_instance;
};

} // namespace osfg
