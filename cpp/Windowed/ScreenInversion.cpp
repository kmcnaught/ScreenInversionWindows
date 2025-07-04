/*************************************************************************************************
*
* File: MagnifierSample.cpp
*
* Description: Implements a simple control that magnifies the screen, using the
* Magnification API. Modified to allow rectangle selection and color inversion.
* Enhanced with configurable shortcuts, dark mode theming, and rectangle save/load.
*
* Modified behavior:
* - Starts maximized without color effects
* - User clicks two points to define a rectangle (one-time operation)
* - Window resizes to selected rectangle size
* - Color inversion is applied by default, with configurable keyboard controls
* - Dark mode title bar and theming
* - Configurable shortcuts via shortcuts.txt file
* - Rectangle save/load: 0-9 to load saved rects, Ctrl+0-9 to save current rect
*
* Requirements: To compile, link to Magnification.lib. The sample must be run with
* elevated privileges. Requires Windows 10 build 17763 or later for dark mode support.
*
*  This file is part of the Microsoft WinfFX SDK Code Samples.
*
*  Copyright (C) Microsoft Corporation.  All rights reserved.
*
*************************************************************************************************/

// Ensure that the following definition is in effect before winuser.h is included.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00    // Windows 10
#endif

#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <magnification.h>
#include <dwmapi.h>
#include <tchar.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>

// Link required libraries
#pragma comment(lib, "dwmapi.lib")

// For simplicity, the sample uses a constant magnification factor.
#define MAGFACTOR  1.0f
#define RESTOREDWINDOWSTYLES WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CAPTION | WS_MAXIMIZEBOX
#define NUM_SAVED_RECTS 10

// Dark mode constants (Windows 10 build 17763+)
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Rectangle selection states
enum SelectionState {
    SELECTION_NONE,
    SELECTION_FIRST_POINT,
    SELECTION_COMPLETE
};

// Configurable shortcuts structure
struct ShortcutConfig {
    UINT toggleInvertKey = 'I';
    UINT toggleGrayscaleKey = 'C';
    UINT cycleWhiteLevelKey = 'W';
    UINT escapeKey = VK_ESCAPE;
    UINT globalHotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
    UINT globalHotkeyKey = 'P';

    // File path for config
    static const char* CONFIG_FILE;
};

const char* ShortcutConfig::CONFIG_FILE = "shortcuts.txt";

// Saved rectangles structure with color settings
struct SavedRectangles {
    RECT rects[NUM_SAVED_RECTS];
    bool isValid[NUM_SAVED_RECTS];

    // Color settings for each saved rectangle
    bool inversionSettings[NUM_SAVED_RECTS];
    bool grayscaleSettings[NUM_SAVED_RECTS];
    int grayLevelSettings[NUM_SAVED_RECTS];

    static const char* RECTS_FILE;

    SavedRectangles() {
        for (int i = 0; i < NUM_SAVED_RECTS; i++) {
            isValid[i] = false;
            memset(&rects[i], 0, sizeof(RECT));
            inversionSettings[i] = false;
            grayscaleSettings[i] = false;
            grayLevelSettings[i] = 0;
        }
    }
};

const char* SavedRectangles::RECTS_FILE = "saved_rects.txt";

// Global variables and strings.
HINSTANCE           hInst;
const TCHAR         WindowClassName[] = TEXT("ScreenFilterWindow");
const TCHAR         WindowTitle[] = TEXT("Screen Filter - Click two points to select area (0=cycle saved, 1-9=load saved)");
const UINT          timerInterval = 16; // close to the refresh rate @60hz
HWND                hwndMag;
HWND                hwndHost;
RECT                magWindowRectClient;
RECT                magWindowRectWindow;
RECT                hostWindowRect;

// Rectangle selection variables
SelectionState      selectionState = SELECTION_NONE;
POINT               firstPoint;
POINT               secondPoint;
RECT                selectedRect;

// Color effect state variables
BOOL                inversionEnabled = FALSE;
BOOL                grayscaleEnabled = FALSE;
int                 grayLevel = 0; // 0-3, representing 4 levels: 100%, 80%, 60%, 40%
BOOL                colorEffectsApplied = FALSE;
BOOL                isPinned = FALSE; // Toggle for click-through behavior
HWND                previousForegroundWindow = NULL; // Track previous focus for unpinning

// Shortcut configuration and saved rectangles
ShortcutConfig      shortcuts;
SavedRectangles     savedRects;
int                 currentCycleSlot = 1; // Start cycling from slot 1

#define HOTKEY_TOGGLE_PIN 1 // Hotkey ID for global shortcut

// Forward declarations.
ATOM                RegisterHostWindowClass(HINSTANCE hInstance);
BOOL                SetupScreenFilter(HINSTANCE hinst);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void CALLBACK       UpdateMagWindow(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void                GoFullScreen();
void                GoPartialScreen();
void                HandleRectangleSelection(POINT clickPoint);
void                ResizeToSelectedRectangle();
void                ApplyColorEffects();
void                CalculateColorMatrix(MAGCOLOREFFECT* matrix);
void                LoadShortcutConfig();
void                SaveDefaultShortcutConfig();
void                ApplyDarkModeToWindow(HWND hwnd);
void                LoadSavedRectangles();
void                SaveSavedRectangles();
void                LoadRectangle(int slot);
void                SaveCurrentRectangle(int slot);
void                CycleToNextSavedRectangle();
void                ApplyLoadedRectangle(const RECT& rect);
BOOL                isFullScreen = FALSE;

//
// FUNCTION: WinMain()
//
// PURPOSE: Entry point for the application.
//
int APIENTRY WinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPSTR     /*lpCmdLine*/,
    _In_ int       nCmdShow)
{
    // Intentionally ignore nCmdShow as we need to start fullscreen 
    // to capture initial points to define rectangle. 
    (void)nCmdShow; 

    if (FALSE == MagInitialize())
    {
        return 0;
    }

    // Load shortcut configuration and saved rectangles
    LoadShortcutConfig();
    LoadSavedRectangles();

    if (FALSE == SetupScreenFilter(hInstance))
    {
        return 0;
    }

    // Apply dark mode theming
    ApplyDarkModeToWindow(hwndHost);

    // Show maximized instead of using nCmdShow
    ShowWindow(hwndHost, SW_MAXIMIZE);
    UpdateWindow(hwndHost);

    // Register global hotkey using configured values
    RegisterHotKey(hwndHost, HOTKEY_TOGGLE_PIN,
        shortcuts.globalHotkeyModifiers, shortcuts.globalHotkeyKey);

    // Create a timer to update the control.
    UINT_PTR timerId = SetTimer(hwndHost, 0, timerInterval, UpdateMagWindow);

    // Main message loop.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Shut down.
    KillTimer(NULL, timerId);
    MagUninitialize();
    return (int)msg.wParam;
}

//
// FUNCTION: LoadSavedRectangles()
//
// PURPOSE: Loads saved rectangle configurations from file.
//
void LoadSavedRectangles()
{
    std::ifstream rectsFile(SavedRectangles::RECTS_FILE);

    if (!rectsFile.is_open())
    {
        // File doesn't exist, initialize with defaults
        return;
    }

    std::string line;
    while (std::getline(rectsFile, line))
    {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos)
        {
            std::string slotStr = line.substr(0, equalPos);
            std::string dataStr = line.substr(equalPos + 1);

            // Trim whitespace
            slotStr.erase(0, slotStr.find_first_not_of(" \t"));
            slotStr.erase(slotStr.find_last_not_of(" \t") + 1);
            dataStr.erase(0, dataStr.find_first_not_of(" \t"));
            dataStr.erase(dataStr.find_last_not_of(" \t") + 1);

            int slot = atoi(slotStr.c_str());
            if (slot >= 0 && slot < NUM_SAVED_RECTS)
            {
                // Parse data: left,top,right,bottom,invert,grayscale,graylevel
                std::stringstream ss(dataStr);
                std::string item;
                std::vector<std::string> items;

                while (std::getline(ss, item, ','))
                {
                    items.push_back(item);
                }

                if (items.size() >= 4)
                {
                    // Rectangle coordinates
                    savedRects.rects[slot].left = atoi(items[0].c_str());
                    savedRects.rects[slot].top = atoi(items[1].c_str());
                    savedRects.rects[slot].right = atoi(items[2].c_str());
                    savedRects.rects[slot].bottom = atoi(items[3].c_str());

                    // Color settings (with backwards compatibility)
                    if (items.size() >= 7)
                    {
                        savedRects.inversionSettings[slot] = (atoi(items[4].c_str()) != 0);
                        savedRects.grayscaleSettings[slot] = (atoi(items[5].c_str()) != 0);
                        savedRects.grayLevelSettings[slot] = atoi(items[6].c_str());
                    }
                    else
                    {
                        // Default color settings for old format
                        savedRects.inversionSettings[slot] = true;
                        savedRects.grayscaleSettings[slot] = false;
                        savedRects.grayLevelSettings[slot] = 0;
                    }

                    savedRects.isValid[slot] = true;
                }
            }
        }
    }

    rectsFile.close();
}

//
// FUNCTION: SaveSavedRectangles()
//
// PURPOSE: Saves current rectangle configurations to file.
//
void SaveSavedRectangles()
{
    std::ofstream rectsFile(SavedRectangles::RECTS_FILE);

    if (!rectsFile.is_open())
        return;

    rectsFile << "# Saved Rectangle Configurations with Color Settings\n";
    rectsFile << "# Format: SlotNumber=Left,Top,Right,Bottom,Invert,Grayscale,GrayLevel\n";
    rectsFile << "# Slots 1-9 available. Use 0 to cycle, 1-9 to load, Ctrl+1-9 to save.\n";
    rectsFile << "# Invert: 1=enabled, 0=disabled\n";
    rectsFile << "# Grayscale: 1=enabled, 0=disabled\n";
    rectsFile << "# GrayLevel: 0=100%, 1=80%, 2=60%, 3=40%\n\n";

    for (int i = 0; i < NUM_SAVED_RECTS; i++)
    {
        if (savedRects.isValid[i])
        {
            rectsFile << i << "="
                << savedRects.rects[i].left << ","
                << savedRects.rects[i].top << ","
                << savedRects.rects[i].right << ","
                << savedRects.rects[i].bottom << ","
                << (savedRects.inversionSettings[i] ? 1 : 0) << ","
                << (savedRects.grayscaleSettings[i] ? 1 : 0) << ","
                << savedRects.grayLevelSettings[i] << "\n";
        }
    }

    rectsFile.close();
}

//
// FUNCTION: LoadRectangle()
//
// PURPOSE: Loads a saved rectangle from the specified slot.
//
void LoadRectangle(int slot)
{
    if (slot < 0 || slot >= NUM_SAVED_RECTS || !savedRects.isValid[slot])
    {
        // Show a brief message that the slot is empty
        TCHAR message[256];
        _stprintf_s(message, 256, TEXT("Screen Filter - Slot %d is empty"), slot);
        SetWindowText(hwndHost, message);

        // Reset title after 2 seconds
        SetTimer(hwndHost, 999, 2000, [](HWND hwnd, UINT, UINT_PTR, DWORD) {
            SetWindowText(hwnd, WindowTitle);
            KillTimer(hwnd, 999);
            });
        return;
    }

    // Restore color settings first
    inversionEnabled = savedRects.inversionSettings[slot];
    grayscaleEnabled = savedRects.grayscaleSettings[slot];
    grayLevel = savedRects.grayLevelSettings[slot];

    ApplyLoadedRectangle(savedRects.rects[slot]);
}

//
// FUNCTION: CycleToNextSavedRectangle()
//
// PURPOSE: Cycles through all saved rectangles, skipping empty slots.
//
void CycleToNextSavedRectangle()
{
    int attempts = 0;

    // Look for the next valid saved rectangle (slots 1-9, skip slot 0)
    do {
        currentCycleSlot++;
        if (currentCycleSlot >= NUM_SAVED_RECTS) {
            currentCycleSlot = 1; // Skip slot 0, start from 1
        }
        attempts++;
    } while (!savedRects.isValid[currentCycleSlot] && attempts < NUM_SAVED_RECTS);

    // If we found a valid slot, load it
    if (savedRects.isValid[currentCycleSlot]) {
        // Restore color settings first
        inversionEnabled = savedRects.inversionSettings[currentCycleSlot];
        grayscaleEnabled = savedRects.grayscaleSettings[currentCycleSlot];
        grayLevel = savedRects.grayLevelSettings[currentCycleSlot];

        ApplyLoadedRectangle(savedRects.rects[currentCycleSlot]);

        // Show which slot was loaded
        TCHAR message[256];
        _stprintf_s(message, 256, TEXT("Screen Filter - Loaded Slot %d (Press 0 to cycle)"), currentCycleSlot);
        SetWindowText(hwndHost, message);

        // Reset title after 2 seconds
        SetTimer(hwndHost, 997, 2000, [](HWND hwnd, UINT, UINT_PTR, DWORD) {
            ApplyColorEffects(); // This will restore the proper title
            KillTimer(hwnd, 997);
            });
    }
    else {
        // No saved rectangles found
        TCHAR message[256];
        _stprintf_s(message, 256, TEXT("Screen Filter - No saved rectangles found (Use Ctrl+1-9 to save)"));
        SetWindowText(hwndHost, message);

        // Reset title after 2 seconds
        SetTimer(hwndHost, 996, 2000, [](HWND hwnd, UINT, UINT_PTR, DWORD) {
            SetWindowText(hwnd, WindowTitle);
            KillTimer(hwnd, 996);
            });
    }
}

//
// FUNCTION: SaveCurrentRectangle()
//
// PURPOSE: Saves the current rectangle and color settings to the specified slot (slots 1-9 only).
//
void SaveCurrentRectangle(int slot)
{
    // Slot 0 is reserved for cycling - cannot save to it
    if (slot <= 0 || slot >= NUM_SAVED_RECTS || selectionState != SELECTION_COMPLETE)
        return;

    // Get the current window position and size, not the original selected rectangle
    RECT currentRect;
    GetWindowRect(hwndHost, &currentRect);

    // Save rectangle
    savedRects.rects[slot] = currentRect;

    // Save current color settings
    savedRects.inversionSettings[slot] = inversionEnabled;
    savedRects.grayscaleSettings[slot] = grayscaleEnabled;
    savedRects.grayLevelSettings[slot] = grayLevel;

    savedRects.isValid[slot] = true;

    SaveSavedRectangles();

    // Show confirmation message
    TCHAR message[256];
    _stprintf_s(message, 256, TEXT("Screen Filter - Rectangle saved to slot %d"), slot);
    SetWindowText(hwndHost, message);

    // Reset title after 2 seconds
    SetTimer(hwndHost, 998, 2000, [](HWND hwnd, UINT, UINT_PTR, DWORD) {
        ApplyColorEffects(); // This will restore the proper title
        KillTimer(hwnd, 998);
        });
}

//
// FUNCTION: ApplyLoadedRectangle()
//
// PURPOSE: Applies a loaded rectangle, completing the selection process.
//
void ApplyLoadedRectangle(const RECT& rect)
{
    // The loaded rectangle is a full window rectangle (including borders and title bar)
    // We need to convert it back to the client area coordinates for selectedRect
    LONG titleBarHeight = GetSystemMetrics(SM_CYCAPTION);
    LONG borderWidth = GetSystemMetrics(SM_CXSIZEFRAME);
    LONG borderHeight = GetSystemMetrics(SM_CYSIZEFRAME);
    
    // Convert window bounds back to client area bounds
    selectedRect.left = rect.left + borderWidth;
    selectedRect.top = rect.top + titleBarHeight + borderHeight;
    selectedRect.right = rect.right - borderWidth;
    selectedRect.bottom = rect.bottom - borderHeight;
    
    selectionState = SELECTION_COMPLETE;

    // Apply the window position directly since rect already contains the proper window bounds
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    
    ShowWindow(hwndHost, SW_RESTORE);
    hostWindowRect = rect;
    SetWindowLong(hwndHost, GWL_STYLE, RESTOREDWINDOWSTYLES);
    SetWindowPos(hwndHost, HWND_TOPMOST, rect.left, rect.top, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ApplyDarkModeToWindow(hwndHost);
    inversionEnabled = TRUE;
    SetWindowLong(hwndHost, GWL_EXSTYLE, GetWindowLong(hwndHost, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwndHost, 0, 255, LWA_ALPHA);
    ApplyColorEffects();

    // Update title to show that a rectangle was loaded
    TCHAR instructionText[256];
    _stprintf_s(instructionText, 256,
        TEXT("Screen Filter - Area Loaded (%c=Invert, %c=Grayscale, %c=White Level, Ctrl+1-9=Save)"),
        shortcuts.toggleInvertKey, shortcuts.toggleGrayscaleKey, shortcuts.cycleWhiteLevelKey);
    SetWindowText(hwndHost, instructionText);
}

//
// FUNCTION: LoadShortcutConfig()
//
// PURPOSE: Loads shortcut configuration from file, creates default if not found.
//
void LoadShortcutConfig()
{
    std::ifstream configFile(ShortcutConfig::CONFIG_FILE);

    if (!configFile.is_open())
    {
        // File doesn't exist, create default configuration
        SaveDefaultShortcutConfig();
        return;
    }

    std::string line;
    std::map<std::string, std::string> configMap;

    // Parse the config file
    while (std::getline(configFile, line))
    {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos)
        {
            std::string key = line.substr(0, equalPos);
            std::string value = line.substr(equalPos + 1);

            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            configMap[key] = value;
        }
    }

    // Apply configuration values
    if (configMap.find("ToggleInvertKey") != configMap.end())
        shortcuts.toggleInvertKey = configMap["ToggleInvertKey"][0];

    if (configMap.find("ToggleGrayscaleKey") != configMap.end())
        shortcuts.toggleGrayscaleKey = configMap["ToggleGrayscaleKey"][0];

    if (configMap.find("CycleWhiteLevelKey") != configMap.end())
        shortcuts.cycleWhiteLevelKey = configMap["CycleWhiteLevelKey"][0];

    if (configMap.find("GlobalHotkeyKey") != configMap.end())
        shortcuts.globalHotkeyKey = configMap["GlobalHotkeyKey"][0];

    // Parse modifier keys
    if (configMap.find("GlobalHotkeyModifiers") != configMap.end())
    {
        std::string modStr = configMap["GlobalHotkeyModifiers"];
        shortcuts.globalHotkeyModifiers = 0;

        if (modStr.find("CTRL") != std::string::npos)
            shortcuts.globalHotkeyModifiers |= MOD_CONTROL;
        if (modStr.find("SHIFT") != std::string::npos)
            shortcuts.globalHotkeyModifiers |= MOD_SHIFT;
        if (modStr.find("ALT") != std::string::npos)
            shortcuts.globalHotkeyModifiers |= MOD_ALT;
        if (modStr.find("WIN") != std::string::npos)
            shortcuts.globalHotkeyModifiers |= MOD_WIN;
    }

    configFile.close();
}

//
// FUNCTION: SaveDefaultShortcutConfig()
//
// PURPOSE: Creates a default shortcut configuration file.
//
void SaveDefaultShortcutConfig()
{
    std::ofstream configFile(ShortcutConfig::CONFIG_FILE);

    if (!configFile.is_open())
        return;

    configFile << "# Screen Filter Shortcut Configuration\n";
    configFile << "# Edit these values to customize keyboard shortcuts\n";
    configFile << "# Use single characters for keys (case sensitive)\n\n";

    configFile << "# Toggle color inversion on/off\n";
    configFile << "ToggleInvertKey=I\n\n";

    configFile << "# Toggle between grayscale and color\n";
    configFile << "ToggleGrayscaleKey=C\n\n";

    configFile << "# Cycle through white/brightness levels\n";
    configFile << "CycleWhiteLevelKey=W\n\n";

    configFile << "# Global hotkey to toggle pin/click-through mode\n";
    configFile << "GlobalHotkeyKey=P\n";
    configFile << "# Modifier keys: CTRL, SHIFT, ALT, WIN (combine with +)\n";
    configFile << "GlobalHotkeyModifiers=CTRL+SHIFT\n\n";

    configFile << "# Note: Restart the application after changing these settings\n";
    configFile << "# Rectangle Save/Load: 0=cycle through saved, 1-9=load saved, Ctrl+1-9=save current (Ctrl+0 disabled)\n";

    configFile.close();
}

//
// FUNCTION: ApplyDarkModeToWindow()
//
// PURPOSE: Applies dark mode theming to the window title bar and frame.
//
void ApplyDarkModeToWindow(HWND hwnd)
{
    // Enable dark mode for the title bar
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // Set dark title bar color (optional, requires Windows 11 22000+)
    COLORREF darkTitleBar = RGB(32, 32, 32);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &darkTitleBar, sizeof(darkTitleBar));

    // Set dark border color
    COLORREF darkBorder = RGB(64, 64, 64);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &darkBorder, sizeof(darkBorder));
}

//
// FUNCTION: HostWndProc()
//
// PURPOSE: Window procedure for the window that hosts the screen filter control.
//
LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCHITTEST:
    {
        // After selection is complete, make client area transparent to clicks
        if (selectionState == SELECTION_COMPLETE)
        {
            LRESULT hitTest = DefWindowProc(hWnd, message, wParam, lParam);

            // Only make the client area transparent, keep frame elements interactive
            if (hitTest == HTCLIENT)
            {
                return HTTRANSPARENT;
            }

            return hitTest;
        }
        else
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_LBUTTONDOWN:
    {
        if (selectionState != SELECTION_COMPLETE)
        {
            POINT clickPoint;
            clickPoint.x = GET_X_LPARAM(lParam);
            clickPoint.y = GET_Y_LPARAM(lParam);

            // Convert to screen coordinates
            ClientToScreen(hWnd, &clickPoint);

            HandleRectangleSelection(clickPoint);
        }
    }
    break;

    case WM_KEYDOWN:
    {
        // Check for Ctrl key state
        BOOL ctrlPressed = GetKeyState(VK_CONTROL) & 0x8000;

        if (wParam == shortcuts.escapeKey)
        {
            if (isFullScreen)
            {
                GoPartialScreen();
            }
        }
        // Cycle through saved rects
        else if (wParam == '0') {
            CycleToNextSavedRectangle();
        }
        // Rectangle save/load 1-9
        else if (wParam > '0' && wParam <= '9')
        {
            int slot = static_cast<int>(wParam) - '0';

            if (ctrlPressed && selectionState == SELECTION_COMPLETE)
            {
                // Save current rectangle to slot
                SaveCurrentRectangle(slot);
            }
            else if (!ctrlPressed && selectionState == SELECTION_NONE)
            {
                // Load rectangle from slot
                LoadRectangle(slot);
            }
        }
        else if (selectionState == SELECTION_COMPLETE)
        {
            // Use configurable shortcuts after selection is complete
            if (wParam == shortcuts.toggleInvertKey)
            {
                inversionEnabled = !inversionEnabled;
                ApplyColorEffects();
            }
            else if (wParam == shortcuts.toggleGrayscaleKey)
            {
                grayscaleEnabled = !grayscaleEnabled;
                ApplyColorEffects();
            }
            else if (wParam == shortcuts.cycleWhiteLevelKey)
            {
                grayLevel = (grayLevel + 1) % 4;
                ApplyColorEffects();
            }
        }
    }
    break;

    case WM_SETFOCUS:
        // Track the previous foreground window when we gain focus
        // (This will be used to restore focus when pinning)
        if (!isPinned) {
            HWND currentForeground = GetForegroundWindow();
            if (currentForeground != hWnd) {
                previousForegroundWindow = currentForeground;
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam);

    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE_PIN && selectionState == SELECTION_COMPLETE)
        {
            // Toggle pin state
            isPinned = !isPinned;

            // Update the window's extended style based on pin state
            LONG exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
            if (isPinned)
            {
                // Store current foreground window before pinning
                previousForegroundWindow = GetForegroundWindow();

                // Add WS_EX_TRANSPARENT when pinned - window becomes click-through
                exStyle |= WS_EX_TRANSPARENT;
                SetWindowLong(hWnd, GWL_EXSTYLE, exStyle);

                // Find window underneath mouse cursor and give it focus
                POINT cursorPos;
                GetCursorPos(&cursorPos);
                HWND windowUnderCursor = WindowFromPoint(cursorPos);

                // If we found a valid window that's not us, set focus to it
                if (windowUnderCursor && windowUnderCursor != hWnd && windowUnderCursor != hwndMag)
                {
                    // Get the top-level parent window
                    HWND topWindow = GetAncestor(windowUnderCursor, GA_ROOT);
                    if (topWindow && topWindow != hWnd)
                    {
                        SetForegroundWindow(topWindow);
                    }
                }
                else if (previousForegroundWindow && IsWindow(previousForegroundWindow))
                {
                    // Fallback to previously focused window
                    SetForegroundWindow(previousForegroundWindow);
                }
            }
            else
            {
                // Remove WS_EX_TRANSPARENT when unpinned - window becomes interactive
                exStyle &= ~WS_EX_TRANSPARENT;
                SetWindowLong(hWnd, GWL_EXSTYLE, exStyle);

                // Restore focus to our window when unpinning
                SetForegroundWindow(hWnd);
            }

            // Update window title to show current pin state
            ApplyColorEffects(); // This will update the title
        }
        break;

    case WM_SYSCOMMAND:
        if (GET_SC_WPARAM(wParam) == SC_MAXIMIZE)
        {
            GoFullScreen();
        }
        else
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;

    case WM_DESTROY:
        // Unregister the global hotkey
        UnregisterHotKey(hwndHost, HOTKEY_TOGGLE_PIN);
        PostQuitMessage(0);
        break;

    case WM_SIZE:
        if (hwndMag != NULL)
        {
            GetClientRect(hWnd, &magWindowRectClient);
            // Resize the control to fill the window.
            SetWindowPos(hwndMag, NULL,
                magWindowRectClient.left, magWindowRectClient.top,
                magWindowRectClient.right - magWindowRectClient.left,
                magWindowRectClient.bottom - magWindowRectClient.top, 0);
        }
        break;

    case WM_WINDOWPOSCHANGED:
        if (hwndMag != NULL)
        {
            GetWindowRect(hWnd, &magWindowRectWindow);
            GetClientRect(hWnd, &magWindowRectClient);
            // Resize the control to fill the window.
            SetWindowPos(hwndMag, NULL,
                magWindowRectClient.left, magWindowRectClient.top,
                magWindowRectClient.right - magWindowRectClient.left,
                magWindowRectClient.bottom - magWindowRectClient.top, 0);
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

//
//  FUNCTION: RegisterHostWindowClass()
//
//  PURPOSE: Registers the window class for the window that contains the screen filter control.
//
ATOM RegisterHostWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = HostWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_CROSS); // Changed to cross cursor for selection
    // Use dark background brush
    wcex.hbrBackground = CreateSolidBrush(RGB(32, 32, 32));
    wcex.lpszClassName = WindowClassName;

    return RegisterClassEx(&wcex);
}

//
// FUNCTION: SetupScreenFilter
//
// PURPOSE: Creates the windows and initializes screen filter.
//
BOOL SetupScreenFilter(HINSTANCE hinst)
{
    // Set bounds of host window to full screen initially
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    hostWindowRect.top = 0;
    hostWindowRect.bottom = height;
    hostWindowRect.left = 0;
    hostWindowRect.right = width;

    // Create the host window.
    RegisterHostWindowClass(hinst);
    hwndHost = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED,
        WindowClassName, WindowTitle,
        RESTOREDWINDOWSTYLES,
        hostWindowRect.left, hostWindowRect.top,
        hostWindowRect.right, hostWindowRect.bottom,
        NULL, NULL, hinst, NULL);
    if (!hwndHost)
    {
        return FALSE;
    }

    // Store instance handle
    hInst = hinst;

    // Make the window opaque.
    SetLayeredWindowAttributes(hwndHost, 0, 255, LWA_ALPHA);

    // Create a magnifier control that fills the client area.
    GetClientRect(hwndHost, &magWindowRectClient);
    hwndMag = CreateWindow(WC_MAGNIFIER, TEXT("ScreenFilterWindow"),
        WS_CHILD | MS_SHOWMAGNIFIEDCURSOR | WS_VISIBLE,
        magWindowRectClient.left, magWindowRectClient.top,
        magWindowRectClient.right - magWindowRectClient.left,
        magWindowRectClient.bottom - magWindowRectClient.top,
        hwndHost, NULL, hinst, NULL);
    if (!hwndMag)
    {
        return FALSE;
    }

    // Set the magnification factor (no magnification, just display).
    MAGTRANSFORM matrix;
    memset(&matrix, 0, sizeof(matrix));
    matrix.v[0][0] = MAGFACTOR;
    matrix.v[1][1] = MAGFACTOR;
    matrix.v[2][2] = 1.0f;

    BOOL ret = MagSetWindowTransform(hwndMag, &matrix);

    // Do NOT apply color inversion initially - wait for rectangle selection

    return ret;
}

//
// FUNCTION: HandleRectangleSelection()
//
// PURPOSE: Handles the two-point rectangle selection process.
//
void HandleRectangleSelection(POINT clickPoint)
{
    switch (selectionState)
    {
    case SELECTION_NONE:
        firstPoint = clickPoint;
        selectionState = SELECTION_FIRST_POINT;
        SetWindowText(hwndHost, TEXT("Screen Filter - Click second point"));
        break;

    case SELECTION_FIRST_POINT:
        secondPoint = clickPoint;
        selectionState = SELECTION_COMPLETE;

        // Calculate the selected rectangle
        selectedRect.left = min(firstPoint.x, secondPoint.x);
        selectedRect.top = min(firstPoint.y, secondPoint.y);
        selectedRect.right = max(firstPoint.x, secondPoint.x);
        selectedRect.bottom = max(firstPoint.y, secondPoint.y);

        // Ensure minimum size
        if ((selectedRect.right - selectedRect.left) < 100)
            selectedRect.right = selectedRect.left + 100;
        if ((selectedRect.bottom - selectedRect.top) < 100)
            selectedRect.bottom = selectedRect.top + 100;

        ResizeToSelectedRectangle();
        ApplyColorEffects();

        // Create shortcut instruction text with current key bindings
        TCHAR instructionText[256];
        _stprintf_s(instructionText, 256,
            TEXT("Screen Filter - Area Selected (%c=Invert, %c=Grayscale, %c=White Level, Ctrl+1-9=Save)"),
            shortcuts.toggleInvertKey, shortcuts.toggleGrayscaleKey, shortcuts.cycleWhiteLevelKey);
        SetWindowText(hwndHost, instructionText);
        break;
    }
}

//
// FUNCTION: ResizeToSelectedRectangle()
//
// PURPOSE: Resizes the application window to match the selected rectangle.
//
void ResizeToSelectedRectangle()
{
    if (selectionState != SELECTION_COMPLETE)
        return;

    // Calculate the desired client area size (the actual magnified content area)
    int clientWidth = selectedRect.right - selectedRect.left;
    int clientHeight = selectedRect.bottom - selectedRect.top;

    // Get the window frame dimensions
    LONG titleBarHeight = GetSystemMetrics(SM_CYCAPTION);
    LONG borderWidth = GetSystemMetrics(SM_CXSIZEFRAME);
    LONG borderHeight = GetSystemMetrics(SM_CYSIZEFRAME);

    // Calculate the total window size needed to achieve the desired client area
    int windowWidth = clientWidth + (2 * borderWidth);
    int windowHeight = clientHeight + titleBarHeight + (2 * borderHeight);

    // Calculate window position so that the CLIENT area matches the selected rectangle
    int windowX = selectedRect.left - borderWidth;
    int windowY = selectedRect.top - titleBarHeight - borderHeight;

    // First, ensure the window is in normal (not maximized) state
    ShowWindow(hwndHost, SW_RESTORE);

    // Update host window rect for future reference (store the actual window bounds)
    hostWindowRect.left = windowX;
    hostWindowRect.top = windowY;
    hostWindowRect.right = windowX + windowWidth;
    hostWindowRect.bottom = windowY + windowHeight;

    // Remove the maximized state and set normal window styles
    SetWindowLong(hwndHost, GWL_STYLE, RESTOREDWINDOWSTYLES);

    // Resize and reposition the window
    SetWindowPos(hwndHost, HWND_TOPMOST,
        windowX, windowY, windowWidth, windowHeight,
        SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Reapply dark mode after style changes
    ApplyDarkModeToWindow(hwndHost);

    // Apply initial color effects (start with inversion enabled by default)
    inversionEnabled = TRUE;
    ApplyColorEffects();

    // Make the window layered and the client area click-through
    SetWindowLong(hwndHost, GWL_EXSTYLE,
        GetWindowLong(hwndHost, GWL_EXSTYLE) | WS_EX_LAYERED);

    // Set the window to be 255 (opaque) but enable per-pixel alpha
    SetLayeredWindowAttributes(hwndHost, 0, 255, LWA_ALPHA);
}

//
// FUNCTION: CalculateColorMatrix()
//
// PURPOSE: Calculates the color transformation matrix based on current settings.
//
void CalculateColorMatrix(MAGCOLOREFFECT* matrix)
{
    // Initialize identity matrix
    memset(matrix, 0, sizeof(MAGCOLOREFFECT));
    matrix->transform[0][0] = 1.0f; // Red
    matrix->transform[1][1] = 1.0f; // Green  
    matrix->transform[2][2] = 1.0f; // Blue
    matrix->transform[3][3] = 1.0f; // Alpha
    matrix->transform[4][4] = 1.0f; // Translation

    // Apply grayscale conversion if enabled
    if (grayscaleEnabled)
    {
        // Luminance weights for RGB to grayscale conversion
        float rWeight = 0.299f;
        float gWeight = 0.587f;
        float bWeight = 0.114f;

        // Set all RGB channels to use the same luminance calculation
        matrix->transform[0][0] = rWeight; matrix->transform[0][1] = rWeight; matrix->transform[0][2] = rWeight;
        matrix->transform[1][0] = gWeight; matrix->transform[1][1] = gWeight; matrix->transform[1][2] = gWeight;
        matrix->transform[2][0] = bWeight; matrix->transform[2][1] = bWeight; matrix->transform[2][2] = bWeight;
    }

    // Apply inversion if enabled
    if (inversionEnabled)
    {
        // Invert RGB channels
        matrix->transform[0][0] *= -1.0f; matrix->transform[0][1] *= -1.0f; matrix->transform[0][2] *= -1.0f;
        matrix->transform[1][0] *= -1.0f; matrix->transform[1][1] *= -1.0f; matrix->transform[1][2] *= -1.0f;
        matrix->transform[2][0] *= -1.0f; matrix->transform[2][1] *= -1.0f; matrix->transform[2][2] *= -1.0f;

        // Add inversion offset
        matrix->transform[4][0] = 1.0f; // Red offset
        matrix->transform[4][1] = 1.0f; // Green offset
        matrix->transform[4][2] = 1.0f; // Blue offset
    }

    // Apply gray level scaling (brightness reduction)
    float grayLevels[] = { 1.0f, 0.8f, 0.6f, 0.4f }; // 100%, 80%, 60%, 40%
    float scale = grayLevels[grayLevel];

    if (scale != 1.0f)
    {
        // Scale RGB channels
        matrix->transform[0][0] *= scale; matrix->transform[0][1] *= scale; matrix->transform[0][2] *= scale;
        matrix->transform[1][0] *= scale; matrix->transform[1][1] *= scale; matrix->transform[1][2] *= scale;
        matrix->transform[2][0] *= scale; matrix->transform[2][1] *= scale; matrix->transform[2][2] *= scale;

        // Scale translation components if inversion is enabled
        if (inversionEnabled)
        {
            matrix->transform[4][0] *= scale;
            matrix->transform[4][1] *= scale;
            matrix->transform[4][2] *= scale;
        }
    }
}

//
// FUNCTION: ApplyColorEffects()
//
// PURPOSE: Applies the current color effect settings to the magnifier.
//
void ApplyColorEffects()
{
    MAGCOLOREFFECT matrix;
    CalculateColorMatrix(&matrix);

    BOOL ret = MagSetColorEffect(hwndMag, &matrix);
    if (ret)
    {
        colorEffectsApplied = TRUE;

        // Update window title to show current settings with current key bindings
        TCHAR titleText[256];

        if (isPinned)
        {
            // When pinned, show unpin instructions using configured hotkey
            TCHAR hotkeyText[64] = TEXT("");

            // Build the hotkey string based on configured modifiers
            if (shortcuts.globalHotkeyModifiers & MOD_CONTROL)
                _tcscat_s(hotkeyText, 64, TEXT("Ctrl+"));
            if (shortcuts.globalHotkeyModifiers & MOD_SHIFT)
                _tcscat_s(hotkeyText, 64, TEXT("Shift+"));
            if (shortcuts.globalHotkeyModifiers & MOD_ALT)
                _tcscat_s(hotkeyText, 64, TEXT("Alt+"));
            if (shortcuts.globalHotkeyModifiers & MOD_WIN)
                _tcscat_s(hotkeyText, 64, TEXT("Win+"));

            // Add the key
            TCHAR keyText[8];
            _stprintf_s(keyText, 8, TEXT("%c"), shortcuts.globalHotkeyKey);
            _tcscat_s(hotkeyText, 64, keyText);

            _stprintf_s(titleText, 256, TEXT("Filter - %s to unpin window"), hotkeyText);
        }
        else
        {
            // When not pinned, show normal color/inversion status
            float grayLevels[] = { 1.0f, 0.8f, 0.6f, 0.4f };
            _stprintf_s(titleText, 256, TEXT("Filter - %s%s Gray:%.0f%% (%c=Invert, %c=Colour, %c=White level, Ctrl+1-9=Save)"),
                inversionEnabled ? TEXT("Inverted ") : TEXT(""),
                grayscaleEnabled ? TEXT("Grayscale ") : TEXT("Color "),
                grayLevels[grayLevel] * 100.0f,
                shortcuts.toggleInvertKey, shortcuts.toggleGrayscaleKey, shortcuts.cycleWhiteLevelKey);
        }

        SetWindowText(hwndHost, titleText);
    }
}

//
// FUNCTION: UpdateMagWindow()
//
// PURPOSE: Sets the source rectangle and updates the window. Called by a timer.
//
void CALLBACK UpdateMagWindow(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/)
{
    RECT sourceRect;

    // Always use the current window position to determine what to show
    GetWindowRect(hwndHost, &magWindowRectWindow);
    GetClientRect(hwndHost, &magWindowRectClient);

    // Get styles for adjustments
    LONG titleBarHeight = GetSystemMetrics(SM_CYCAPTION);
    LONG borderWidth = GetSystemMetrics(SM_CXSIZEFRAME);
    LONG borderHeight = GetSystemMetrics(SM_CYSIZEFRAME);

    int fudge = 4;

    sourceRect.left = magWindowRectWindow.left + magWindowRectClient.left + borderWidth + fudge;
    sourceRect.top = magWindowRectWindow.top + magWindowRectClient.top + titleBarHeight + borderHeight + fudge;

    // Calculate the width and height based on client area size
    int width = (int)((magWindowRectWindow.right - magWindowRectWindow.left) / MAGFACTOR);
    int height = (int)((magWindowRectWindow.bottom - magWindowRectWindow.top) / MAGFACTOR);

    sourceRect.right = sourceRect.left + width;
    sourceRect.bottom = sourceRect.top + height;

    // Set the source rectangle for the magnifier control.
    MagSetWindowSource(hwndMag, sourceRect);

    // Reclaim topmost status, to prevent unmagnified menus from remaining in view. 
    SetWindowPos(hwndHost, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

    // Force redraw.
    InvalidateRect(hwndMag, NULL, TRUE);
}

//
// FUNCTION: GoFullScreen()
//
// PURPOSE: Makes the host window full-screen by placing non-client elements outside the display.
//
void GoFullScreen()
{
    isFullScreen = TRUE;

    // The window must be styled as layered for proper rendering. 
    // It is styled as transparent so that it does not capture mouse clicks.
    SetWindowLong(hwndHost, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT);

    // Give the window a system menu so it can be closed on the taskbar.
    SetWindowLong(hwndHost, GWL_STYLE, WS_CAPTION | WS_SYSMENU);

    // Calculate the span of the display area.
    HDC hDC = GetDC(NULL);
    int xSpan = GetSystemMetrics(SM_CXSCREEN);
    int ySpan = GetSystemMetrics(SM_CYSCREEN);
    ReleaseDC(NULL, hDC);

    // Calculate the size of system elements.
    int xBorder = GetSystemMetrics(SM_CXFRAME);
    int yCaption = GetSystemMetrics(SM_CYCAPTION);
    int yBorder = GetSystemMetrics(SM_CYFRAME);

    // Calculate the window origin and span for full-screen mode.
    int xOrigin = -xBorder;
    int yOrigin = -yBorder - yCaption;
    xSpan += 2 * xBorder;
    ySpan += 2 * yBorder + yCaption;

    SetWindowPos(hwndHost, HWND_TOPMOST, xOrigin, yOrigin, xSpan, ySpan,
        SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
}

//
// FUNCTION: GoPartialScreen()
//
// PURPOSE: Makes the host window resizable and focusable.
//
void GoPartialScreen()
{
    isFullScreen = FALSE;

    SetWindowLong(hwndHost, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED);
    SetWindowLong(hwndHost, GWL_STYLE, RESTOREDWINDOWSTYLES);
    SetWindowPos(hwndHost, HWND_TOPMOST,
        hostWindowRect.left, hostWindowRect.top,
        hostWindowRect.right, hostWindowRect.bottom,
        SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);

    // Reapply dark mode after style changes
    ApplyDarkModeToWindow(hwndHost);
}