/*************************************************************************************************
*
* File: MagnifierSample.cpp
*
* Description: Implements a simple control that magnifies the screen, using the
* Magnification API. Modified to allow rectangle selection and color inversion.
*
* Modified behavior:
* - Starts maximized without color effects
* - User clicks two points to define a rectangle (one-time operation)
* - Window resizes to selected rectangle size
* - Color inversion is applied by default, with keyboard controls:
*   - I key: Toggle inversion on/off
*   - C key: Toggle grayscale vs color
*   - G key: Cycle through 5 gray levels (100%, 80%, 60%, 40%, 20%)
* - After selection: WM_NCHITTEST + layered window makes content click-through, frame interactive
*
* Requirements: To compile, link to Magnification.lib. The sample must be run with
* elevated privileges.
*
*  This file is part of the Microsoft WinfFX SDK Code Samples.
*
*  Copyright (C) Microsoft Corporation.  All rights reserved.
*
*************************************************************************************************/

// Ensure that the following definition is in effect before winuser.h is included.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501    
#endif

#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <magnification.h>
#include <tchar.h>
#include <stdio.h>

// For simplicity, the sample uses a constant magnification factor.
#define MAGFACTOR  1.0f
#define RESTOREDWINDOWSTYLES WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CAPTION | WS_MAXIMIZEBOX

// Rectangle selection states
enum SelectionState {
    SELECTION_NONE,
    SELECTION_FIRST_POINT,
    SELECTION_COMPLETE
};

// Global variables and strings.
HINSTANCE           hInst;
const TCHAR         WindowClassName[] = TEXT("MagnifierWindow");
const TCHAR         WindowTitle[] = TEXT("Screen Magnifier - Click two points to select area");
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
int                 grayLevel = 0; // 0-4, representing 5 levels: 100%, 80%, 60%, 40%, 20%
BOOL                colorEffectsApplied = FALSE;

// Forward declarations.
ATOM                RegisterHostWindowClass(HINSTANCE hInstance);
BOOL                SetupMagnifier(HINSTANCE hinst);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void CALLBACK       UpdateMagWindow(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void                GoFullScreen();
void                GoPartialScreen();
void                HandleRectangleSelection(POINT clickPoint);
void                ResizeToSelectedRectangle();
void                ApplyColorEffects();
void                CalculateColorMatrix(MAGCOLOREFFECT* matrix);
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
    if (FALSE == MagInitialize())
    {
        return 0;
    }
    if (FALSE == SetupMagnifier(hInstance))
    {
        return 0;
    }

    // Show maximized instead of using nCmdShow
    ShowWindow(hwndHost, SW_MAXIMIZE);
    UpdateWindow(hwndHost);

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
// FUNCTION: HostWndProc()
//
// PURPOSE: Window procedure for the window that hosts the magnifier control.
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
        if (wParam == VK_ESCAPE)
        {
            if (isFullScreen)
            {
                GoPartialScreen();
            }
        }
        else if (selectionState == SELECTION_COMPLETE)
        {
            // Only allow color shortcuts after selection is complete
            switch (wParam)
            {
            case 'I': // Toggle inversion
                inversionEnabled = !inversionEnabled;
                ApplyColorEffects();
                break;
            case 'C': // Toggle grayscale vs color
                grayscaleEnabled = !grayscaleEnabled;
                ApplyColorEffects();
                break;
            case 'G': // Cycle through gray levels (5 settings: 100%, 80%, 60%, 40%, 20%)
                grayLevel = (grayLevel + 1) % 5;
                ApplyColorEffects();
                break;
            }
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
//  PURPOSE: Registers the window class for the window that contains the magnification control.
//
ATOM RegisterHostWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = HostWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_CROSS); // Changed to cross cursor for selection
    wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
    wcex.lpszClassName = WindowClassName;

    return RegisterClassEx(&wcex);
}

//
// FUNCTION: SetupMagnifier
//
// PURPOSE: Creates the windows and initializes magnification.
//
BOOL SetupMagnifier(HINSTANCE hinst)
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
    hwndMag = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"),
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
        SetWindowText(hwndHost, TEXT("Screen Magnifier - Click second point"));
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

        SetWindowText(hwndHost, TEXT("Screen Magnifier - Area Selected (I=Invert, C=Grayscale, G=Gray Level)"));
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

    int width = selectedRect.right - selectedRect.left;
    int height = selectedRect.bottom - selectedRect.top;

    // First, ensure the window is in normal (not maximized) state
    ShowWindow(hwndHost, SW_RESTORE);

    // Update host window rect for future reference
    hostWindowRect = selectedRect;

    // Remove the maximized state and set normal window styles
    SetWindowLong(hwndHost, GWL_STYLE, RESTOREDWINDOWSTYLES);

    // Resize and reposition the window
    SetWindowPos(hwndHost, HWND_TOPMOST,
        selectedRect.left, selectedRect.top, width, height,
        SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);

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
// FUNCTION: CreateClickThroughRegion()
//
// PURPOSE: Creates a window region that excludes the client area for click-through behavior.
//
void CreateClickThroughRegion()
{
    RECT windowRect, clientRect;
    GetWindowRect(hwndHost, &windowRect);
    GetClientRect(hwndHost, &clientRect);

    // Get frame dimensions
    int frameWidth = GetSystemMetrics(SM_CXSIZEFRAME);
    int frameHeight = GetSystemMetrics(SM_CYSIZEFRAME);
    int captionHeight = GetSystemMetrics(SM_CYCAPTION);

    // Window dimensions (in window coordinates, so starts at 0,0)
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    // Client area position within the window
    int clientLeft = frameWidth;
    int clientTop = captionHeight + frameHeight;
    int clientRight = windowWidth - frameWidth;
    int clientBottom = windowHeight - frameHeight;

    // Create the complete window region
    HRGN windowRgn = CreateRectRgn(0, 0, windowWidth, windowHeight);

    // Create the client area region to exclude
    HRGN clientRgn = CreateRectRgn(clientLeft, clientTop, clientRight, clientBottom);

    // Subtract the client area from the window region
    CombineRgn(windowRgn, windowRgn, clientRgn, RGN_DIFF);

    // Apply the region to the window
    SetWindowRgn(hwndHost, windowRgn, TRUE);

    // Clean up (windowRgn is now owned by the window)
    DeleteObject(clientRgn);
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
        matrix->transform[0][0] = rWeight; matrix->transform[0][1] = gWeight; matrix->transform[0][2] = bWeight;
        matrix->transform[1][0] = rWeight; matrix->transform[1][1] = gWeight; matrix->transform[1][2] = bWeight;
        matrix->transform[2][0] = rWeight; matrix->transform[2][1] = gWeight; matrix->transform[2][2] = bWeight;
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
    float grayLevels[] = { 1.0f, 0.8f, 0.6f, 0.4f, 0.2f }; // 100%, 80%, 60%, 40%, 20%
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

        // Update window title to show current settings
        TCHAR titleText[256];
        float grayLevels[] = { 1.0f, 0.8f, 0.6f, 0.4f, 0.2f };

        _stprintf_s(titleText, 256, TEXT("Magnifier - %s%s Gray:%.0f%% (I=Invert, C=Grayscale, G=Gray Level)"),
            inversionEnabled ? TEXT("Inverted ") : TEXT(""),
            grayscaleEnabled ? TEXT("Grayscale ") : TEXT("Color "),
            grayLevels[grayLevel] * 100.0f);

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
}