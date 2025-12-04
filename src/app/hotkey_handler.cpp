// OSFG - Open Source Frame Generation
// Hotkey Handler Implementation

#include "hotkey_handler.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <sstream>

namespace osfg {

// Static instance pointer for window procedure
HotkeyHandler* HotkeyHandler::s_instance = nullptr;

std::string HotkeyBinding::ToString() const {
    std::stringstream ss;

    if (HasModifier(modifiers, ModifierKey::Ctrl)) ss << "Ctrl+";
    if (HasModifier(modifiers, ModifierKey::Alt)) ss << "Alt+";
    if (HasModifier(modifiers, ModifierKey::Shift)) ss << "Shift+";
    if (HasModifier(modifiers, ModifierKey::Win)) ss << "Win+";

    ss << HotkeyHandler::VirtualKeyToString(virtualKey);

    return ss.str();
}

HotkeyHandler::HotkeyHandler() = default;

HotkeyHandler::~HotkeyHandler() {
    Shutdown();
}

bool HotkeyHandler::Initialize() {
    if (m_initialized) {
        return true;
    }

    s_instance = this;

    // Create a hidden message window for receiving hotkey messages
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MessageWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"OSFGHotkeyHandler";

    if (!RegisterClassExW(&wc)) {
        // Class may already be registered
        DWORD lastErr = ::GetLastError();
        if (lastErr != ERROR_CLASS_ALREADY_EXISTS) {
            m_lastError = "Failed to register window class";
            return false;
        }
    }

    m_messageWindow = CreateWindowExW(
        0, L"OSFGHotkeyHandler", L"OSFG Hotkey Handler",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
        GetModuleHandle(nullptr), nullptr);

    if (!m_messageWindow) {
        m_lastError = "Failed to create message window";
        return false;
    }

    m_initialized = true;
    return true;
}

void HotkeyHandler::Shutdown() {
    UnregisterAllHotkeys();

    if (m_messageWindow) {
        DestroyWindow(m_messageWindow);
        m_messageWindow = nullptr;
    }

    s_instance = nullptr;
    m_initialized = false;
}

bool HotkeyHandler::RegisterHotkey(const HotkeyBinding& binding) {
    if (!m_initialized) {
        m_lastError = "Not initialized";
        return false;
    }

    if (!binding.enabled) {
        return true; // Skip disabled bindings
    }

    // Unregister existing binding for this action if any
    auto it = m_actionToId.find(binding.action);
    if (it != m_actionToId.end()) {
        UnregisterSystemHotkey(it->second);
        m_bindings.erase(it->second);
        m_actionToId.erase(it);
    }

    // Register the new hotkey
    if (!RegisterSystemHotkey(binding)) {
        return false;
    }

    int id = binding.GetId();
    m_bindings[id] = binding;
    m_actionToId[binding.action] = id;

    return true;
}

bool HotkeyHandler::UnregisterHotkey(HotkeyAction action) {
    auto it = m_actionToId.find(action);
    if (it == m_actionToId.end()) {
        return false;
    }

    UnregisterSystemHotkey(it->second);
    m_bindings.erase(it->second);
    m_actionToId.erase(it);

    return true;
}

void HotkeyHandler::UnregisterAllHotkeys() {
    for (const auto& pair : m_bindings) {
        UnregisterSystemHotkey(pair.first);
    }
    m_bindings.clear();
    m_actionToId.clear();
}

void HotkeyHandler::SetCallback(HotkeyCallback callback) {
    m_callback = callback;
}

bool HotkeyHandler::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != WM_HOTKEY) {
        return false;
    }

    int id = static_cast<int>(wParam);
    auto it = m_bindings.find(id);
    if (it != m_bindings.end() && m_callback) {
        m_callback(it->second.action);
        return true;
    }

    return false;
}

void HotkeyHandler::RegisterDefaultHotkeys(uint32_t toggleFrameGen, uint32_t toggleOverlay,
                                            uint32_t cycleMode, bool requireAlt) {
    ModifierKey modifiers = requireAlt ? ModifierKey::Alt : ModifierKey::None;

    HotkeyBinding binding;
    binding.modifiers = modifiers;

    // Toggle frame generation
    binding.virtualKey = toggleFrameGen;
    binding.action = HotkeyAction::ToggleFrameGen;
    RegisterHotkey(binding);

    // Toggle overlay
    binding.virtualKey = toggleOverlay;
    binding.action = HotkeyAction::ToggleOverlay;
    RegisterHotkey(binding);

    // Cycle mode
    binding.virtualKey = cycleMode;
    binding.action = HotkeyAction::CycleMode;
    RegisterHotkey(binding);
}

const HotkeyBinding* HotkeyHandler::GetBinding(HotkeyAction action) const {
    auto it = m_actionToId.find(action);
    if (it == m_actionToId.end()) {
        return nullptr;
    }

    auto bindingIt = m_bindings.find(it->second);
    if (bindingIt == m_bindings.end()) {
        return nullptr;
    }

    return &bindingIt->second;
}

bool HotkeyHandler::RegisterSystemHotkey(const HotkeyBinding& binding) {
    int id = binding.GetId();
    uint32_t modifiers = ModifiersToWin32(binding.modifiers);

    if (!RegisterHotKey(m_messageWindow, id, modifiers, binding.virtualKey)) {
        DWORD lastErr = ::GetLastError();
        if (lastErr == ERROR_HOTKEY_ALREADY_REGISTERED) {
            m_lastError = "Hotkey " + binding.ToString() + " is already registered by another application";
        } else {
            std::stringstream ss;
            ss << "Failed to register hotkey " << binding.ToString() << " (error " << lastErr << ")";
            m_lastError = ss.str();
        }
        return false;
    }

    return true;
}

void HotkeyHandler::UnregisterSystemHotkey(int id) {
    UnregisterHotKey(m_messageWindow, id);
}

uint32_t HotkeyHandler::ModifiersToWin32(ModifierKey modifiers) const {
    uint32_t result = 0;

    if (HasModifier(modifiers, ModifierKey::Alt)) result |= MOD_ALT;
    if (HasModifier(modifiers, ModifierKey::Ctrl)) result |= MOD_CONTROL;
    if (HasModifier(modifiers, ModifierKey::Shift)) result |= MOD_SHIFT;
    if (HasModifier(modifiers, ModifierKey::Win)) result |= MOD_WIN;

    // Allow repeat by default
    result |= MOD_NOREPEAT;

    return result;
}

std::string HotkeyHandler::VirtualKeyToString(uint32_t vk) {
    switch (vk) {
        case VK_F1: return "F1";
        case VK_F2: return "F2";
        case VK_F3: return "F3";
        case VK_F4: return "F4";
        case VK_F5: return "F5";
        case VK_F6: return "F6";
        case VK_F7: return "F7";
        case VK_F8: return "F8";
        case VK_F9: return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";
        case VK_ESCAPE: return "Escape";
        case VK_TAB: return "Tab";
        case VK_CAPITAL: return "CapsLock";
        case VK_SPACE: return "Space";
        case VK_RETURN: return "Enter";
        case VK_BACK: return "Backspace";
        case VK_DELETE: return "Delete";
        case VK_INSERT: return "Insert";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        case VK_NUMPAD0: return "Num0";
        case VK_NUMPAD1: return "Num1";
        case VK_NUMPAD2: return "Num2";
        case VK_NUMPAD3: return "Num3";
        case VK_NUMPAD4: return "Num4";
        case VK_NUMPAD5: return "Num5";
        case VK_NUMPAD6: return "Num6";
        case VK_NUMPAD7: return "Num7";
        case VK_NUMPAD8: return "Num8";
        case VK_NUMPAD9: return "Num9";
        case VK_MULTIPLY: return "Num*";
        case VK_ADD: return "Num+";
        case VK_SUBTRACT: return "Num-";
        case VK_DECIMAL: return "Num.";
        case VK_DIVIDE: return "Num/";
        case VK_PAUSE: return "Pause";
        case VK_SCROLL: return "ScrollLock";
        case VK_SNAPSHOT: return "PrintScreen";
        default:
            // For letter and number keys
            if (vk >= 'A' && vk <= 'Z') {
                return std::string(1, static_cast<char>(vk));
            }
            if (vk >= '0' && vk <= '9') {
                return std::string(1, static_cast<char>(vk));
            }
            // Unknown key
            std::stringstream ss;
            ss << "0x" << std::hex << vk;
            return ss.str();
    }
}

uint32_t HotkeyHandler::StringToVirtualKey(const std::string& str) {
    if (str.empty()) return 0;

    // Function keys
    if (str.length() >= 2 && (str[0] == 'F' || str[0] == 'f')) {
        int num = std::atoi(str.c_str() + 1);
        if (num >= 1 && num <= 12) {
            return VK_F1 + num - 1;
        }
    }

    // Single character
    if (str.length() == 1) {
        char c = str[0];
        if (c >= 'a' && c <= 'z') return static_cast<uint32_t>(c - 32);
        if (c >= 'A' && c <= 'Z') return static_cast<uint32_t>(c);
        if (c >= '0' && c <= '9') return static_cast<uint32_t>(c);
    }

    // Named keys
    std::string lower = str;
    for (auto& c : lower) c = static_cast<char>(tolower(c));

    if (lower == "escape" || lower == "esc") return VK_ESCAPE;
    if (lower == "tab") return VK_TAB;
    if (lower == "space") return VK_SPACE;
    if (lower == "enter" || lower == "return") return VK_RETURN;
    if (lower == "backspace") return VK_BACK;
    if (lower == "delete" || lower == "del") return VK_DELETE;
    if (lower == "insert" || lower == "ins") return VK_INSERT;
    if (lower == "home") return VK_HOME;
    if (lower == "end") return VK_END;
    if (lower == "pageup" || lower == "pgup") return VK_PRIOR;
    if (lower == "pagedown" || lower == "pgdn") return VK_NEXT;
    if (lower == "up") return VK_UP;
    if (lower == "down") return VK_DOWN;
    if (lower == "left") return VK_LEFT;
    if (lower == "right") return VK_RIGHT;
    if (lower == "pause") return VK_PAUSE;
    if (lower == "printscreen" || lower == "prtsc") return VK_SNAPSHOT;

    return 0;
}

LRESULT CALLBACK HotkeyHandler::MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY && s_instance) {
        s_instance->ProcessMessage(msg, wParam, lParam);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace osfg
