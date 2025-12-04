# Configuration Guide

OSFG uses an INI-based configuration file for persistent settings. This guide covers all available options.

## Configuration File Location

The configuration file is automatically created at:
```
%APPDATA%\OSFG\config.ini
```

Example path: `C:\Users\<username>\AppData\Roaming\OSFG\config.ini`

## Configuration Sections

### [FrameGen]

Controls frame generation behavior.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `Mode` | string | `2X` | Frame generation multiplier: `Disabled`, `2X`, `3X`, `4X` |
| `Enabled` | bool | `true` | Enable/disable frame generation |
| `TargetFramerate` | float | `0` | Target output framerate (0 = match display) |

```ini
[FrameGen]
Mode = 2X
Enabled = true
TargetFramerate = 0
```

### [Capture]

Controls frame capture behavior.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `Method` | string | `Auto` | Capture method: `Auto`, `DXGI`, `WGC` |
| `Monitor` | int | `0` | Monitor index to capture (0 = primary) |
| `Cursor` | bool | `true` | Include cursor in capture |

```ini
[Capture]
Method = Auto
Monitor = 0
Cursor = true
```

### [GPU]

Controls GPU selection and dual-GPU mode.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `Mode` | string | `Auto` | GPU mode: `Auto`, `Single`, `Dual` |
| `Primary` | int | `0` | Primary GPU adapter index |
| `Secondary` | int | `1` | Secondary GPU adapter index (for dual-GPU) |

```ini
[GPU]
Mode = Auto
Primary = 0
Secondary = 1
```

**Finding GPU Indices:**

Run `test_dxgi_capture.exe` to see available adapters:
```
Available adapters:
  0: NVIDIA GeForce RTX 3080
  1: NVIDIA GeForce GTX 1080
  2: Intel UHD Graphics 630
```

### [OpticalFlow]

Controls motion estimation parameters.

| Key | Type | Default | Range | Description |
|-----|------|---------|-------|-------------|
| `BlockSize` | int | `8` | 4-32 | Block size for matching (pixels) |
| `SearchRadius` | int | `12` | 4-24 | Search radius (pixels) |
| `SceneChangeThreshold` | float | `0.5` | 0.0-1.0 | Scene change detection sensitivity |

```ini
[OpticalFlow]
BlockSize = 8
SearchRadius = 12
SceneChangeThreshold = 0.5
```

**Tuning Tips:**
- Larger `BlockSize`: Faster but less accurate
- Larger `SearchRadius`: Better for fast motion, slower
- Lower `SceneChangeThreshold`: More sensitive to cuts

### [Presentation]

Controls output display settings.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `VSync` | bool | `true` | Enable vertical sync |
| `Borderless` | bool | `true` | Use borderless window |
| `Width` | int | `1920` | Output window width |
| `Height` | int | `1080` | Output window height |

```ini
[Presentation]
VSync = true
Borderless = true
Width = 1920
Height = 1080
```

### [Overlay]

Controls the statistics overlay display.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `Show` | bool | `true` | Show overlay |
| `FPS` | bool | `true` | Show FPS counter |
| `FrameTime` | bool | `true` | Show frame times |
| `GPUUsage` | bool | `false` | Show GPU utilization |
| `Position` | int | `0` | Position: 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight |
| `Scale` | float | `1.0` | Overlay scale factor |

```ini
[Overlay]
Show = true
FPS = true
FrameTime = true
GPUUsage = false
Position = 0
Scale = 1.0
```

### [Hotkeys]

Controls keyboard shortcuts. Values are Windows Virtual Key codes.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `ToggleFrameGen` | int | `121` | Toggle frame generation (VK_F10) |
| `ToggleOverlay` | int | `122` | Toggle overlay (VK_F11) |
| `CycleMode` | int | `123` | Cycle mode (VK_F12) |
| `RequireAlt` | bool | `true` | Require Alt modifier |

```ini
[Hotkeys]
ToggleFrameGen = 121
ToggleOverlay = 122
CycleMode = 123
RequireAlt = true
```

**Common Virtual Key Codes:**

| Key | Code | Key | Code |
|-----|------|-----|------|
| F1 | 112 | F7 | 118 |
| F2 | 113 | F8 | 119 |
| F3 | 114 | F9 | 120 |
| F4 | 115 | F10 | 121 |
| F5 | 116 | F11 | 122 |
| F6 | 117 | F12 | 123 |

### [Advanced]

Advanced settings for power users.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `FrameBufferCount` | int | `3` | Number of frame buffers (2-4) |
| `PeerToPeer` | bool | `true` | Use peer-to-peer GPU transfer |
| `Debug` | bool | `false` | Enable debug logging |
| `LogFile` | string | `""` | Path to log file |

```ini
[Advanced]
FrameBufferCount = 3
PeerToPeer = true
Debug = false
LogFile = ""
```

## Complete Example Configuration

```ini
# OSFG Configuration File

[FrameGen]
Mode = 2X
Enabled = true
TargetFramerate = 0

[Capture]
Method = Auto
Monitor = 0
Cursor = true

[GPU]
Mode = Dual
Primary = 0
Secondary = 1

[OpticalFlow]
BlockSize = 8
SearchRadius = 12
SceneChangeThreshold = 0.5

[Presentation]
VSync = true
Borderless = true
Width = 1920
Height = 1080

[Overlay]
Show = true
FPS = true
FrameTime = true
GPUUsage = true
Position = 0
Scale = 1.0

[Hotkeys]
ToggleFrameGen = 121
ToggleOverlay = 122
CycleMode = 123
RequireAlt = true

[Advanced]
FrameBufferCount = 3
PeerToPeer = true
Debug = false
LogFile = ""
```

## Runtime Configuration

Settings can be changed at runtime using hotkeys:

| Action | Default Hotkey |
|--------|----------------|
| Toggle frame generation | Alt+F10 |
| Toggle overlay | Alt+F11 |
| Cycle mode (2X→3X→4X→2X) | Alt+F12 |

Changes made via hotkeys are saved to the config file automatically.

## Programmatic Configuration

```cpp
#include "app/config_manager.h"

// Get singleton instance
auto& config = osfg::ConfigManager::Instance();

// Load configuration
config.Load();

// Access settings
const auto& settings = config.GetSettings();
bool frameGenEnabled = settings.enableFrameGen;

// Modify settings
auto& mutableSettings = config.GetSettingsMutable();
mutableSettings.frameGenMode = osfg::FrameGenMode::FrameGen3X;
config.ApplySettings();

// Save to file
config.Save();
```

## Troubleshooting

### Config file not created

Ensure the application has write permissions to `%APPDATA%`. Run as administrator if needed.

### Settings not persisting

Check for syntax errors in the INI file. Comments must start with `#` or `;`.

### Hotkeys not working

- Ensure no other application has registered the same hotkey
- Try disabling `RequireAlt` if Alt key conflicts exist
- Check Windows Focus Assist settings
