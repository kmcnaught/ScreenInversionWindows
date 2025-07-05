# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Windows screen inversion/filter utility built using C++ and the Windows Magnification API. The project is based on Microsoft's Windows Magnification Sample but has been extensively modified to create a screen filter window that can invert colors and adjust brightness of a selected screen portion.

## Key Features

- **Screen Rectangle Selection**: User clicks two points to define a rectangle for filtering
- **Color Inversion**: Applies color inversion effects to the selected screen area
- **Configurable Shortcuts**: Keyboard shortcuts defined in `shortcuts.txt` file
- **Rectangle Save/Load**: Save rectangles with Ctrl+0-9, load with 0-9 keys
- **Dark Mode Support**: Windows 10+ dark mode theming for title bar
- **Brightness Controls**: Configurable keyboard controls for brightness adjustment

## Build System

This project uses Visual Studio with MSBuild:

- **Solution File**: `cpp/MagnifierSample.sln`
- **Project File**: `cpp/Windowed/MagnifierSample.vcxproj`
- **Main Source**: `cpp/Windowed/ScreenInversion.cpp`

### Build Commands

```bash
# Build Debug version (x64)
msbuild cpp/MagnifierSample.sln /p:Configuration=Debug /p:Platform=x64

# Build Release version (x64)
msbuild cpp/MagnifierSample.sln /p:Configuration=Release /p:Platform=x64

# Build from Visual Studio
# Open cpp/MagnifierSample.sln in Visual Studio and build
```

### Build Requirements

- Visual Studio 2022 (Platform Toolset v143)
- Windows 10 SDK (minimum build 17763 for dark mode support)
- Elevated privileges required for execution
- Links against `magnification.lib` and `dwmapi.lib`

## Architecture

### Core Components

- **Main Window**: Full-screen magnifier window that starts maximized
- **Rectangle Selection**: Two-click selection system to define filter area
- **Color Processing**: Real-time color inversion and brightness adjustment
- **Configuration System**: Text-based configuration files for shortcuts and saved rectangles

### Key Files

- `cpp/Windowed/ScreenInversion.cpp`: Main application logic, window management, magnification API usage
- `cpp/Windowed/shortcuts.txt`: Keyboard shortcut configuration
- `cpp/Windowed/saved_rects.txt`: Saved rectangle coordinates storage

### Dependencies

- Windows Magnification API (`magnification.lib`)
- Desktop Window Manager API (`dwmapi.lib`)
- Standard Windows APIs (User32, GDI32, etc.)

## Configuration Files

### shortcuts.txt Format
Key-value pairs defining keyboard shortcuts for various functions (brightness, contrast, etc.)

### saved_rects.txt Format
Stores rectangle coordinates for quick recall (0-9 slots)

## Development Notes

- Project originally named "MagnifierSample" but renamed to "ScreenFilterWindow"
- Requires elevated privileges due to Magnification API requirements
- Uses Windows 10+ APIs for dark mode support
- Targets x64 platform primarily
- Warning level set to Level4 with warnings treated as errors (except Debug x64 configuration)

## Output Locations

- Debug builds: `cpp/x64/Debug/`
- Release builds: `cpp/x64/Release/`
- Final executable names vary by configuration (MagnifierSample.exe, ScreenFilterWindow.exe, ScreenInversionWindow.exe)