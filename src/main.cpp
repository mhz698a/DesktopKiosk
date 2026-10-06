#include "SystemUtils.h"
#include "SessionWindow.h"
#include "WebViewManager.h"

#include <windows.h>

#include <iostream>
#include <string>


// =============================================
// Estado del kiosco
// =============================================

enum class KioskState { Locked, Unlocked };

void LockKiosk();
void CloseKiosk();

// Estado actual del programa.
// Al iniciar, el kiosco comienza bloqueado.
KioskState g_kioskState = KioskState::Locked;

// =============================================
// Variables globales de Win32 / WebView2
// =============================================

HWND g_hwnd = nullptr;

// =============================================
// Funciones del kiosco
// =============================================

void UnlockKiosk()
{
    g_kioskState = KioskState::Unlocked;

    OutputDebugStringW(L"[KIOSK] Estado cambiado a UNLOCKED\n");

    ShowWindow(g_hwnd, SW_HIDE);

    CreateSessionWindow(reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE)));
}

void LockKiosk()
{
    g_kioskState = KioskState::Locked;

    OutputDebugStringW(L"[KIOSK] Estado cambiado a LOCKED\n");

    DestroySessionWindow();

    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

void CloseKiosk()
{
    DestroySessionWindow();
    if (g_hwnd != nullptr) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
}


// =============================================
// Window Procedure
// =============================================

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_SIZE:
        {
            ResizeWebView();
            return 0;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// =============================================
// WinMain
// =============================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // --------------------------------------------------------
    // Registrar clase de ventana
    // --------------------------------------------------------

    const wchar_t CLASS_NAME[] = L"DesktopKioskWindow";
    WNDCLASSW windowClass = {};

    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = CLASS_NAME;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&windowClass);

    // --------------------------------------------------------
    // Crear ventana
    // --------------------------------------------------------

    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, CLASS_NAME, L"DesktopKiosk",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 800,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hwnd)
    {
        MessageBoxW(nullptr, L"No se pudo crear la ventana.", L"DesktopKiosk", MB_ICONERROR);
        return 1;
    }

    // --------------------------------------------------------
    // Mostrar ventana
    // --------------------------------------------------------

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    // --------------------------------------------------------
    // Inicializar WebView2
    // --------------------------------------------------------

    InitializeWebView2(g_hwnd);

    // --------------------------------------------------------
    // Message Loop
    // --------------------------------------------------------

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}