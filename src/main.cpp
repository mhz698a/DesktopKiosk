#include "SystemUtils.h"
#include "SessionWindow.h"
#include "WebViewManager.h"
#include "HttpServer.h"
#include "KioskMessages.h"

#include <windows.h>

#include <QApplication>

#include <iostream>
#include <string>

enum class KioskState { Locked, Unlocked };

void LockKiosk();
void CloseKiosk();

KioskState g_kioskState = KioskState::Locked;
HWND g_hwnd = nullptr;

namespace
{
constexpr UINT_PTR kKioskIdleTimerId = 7085;
constexpr DWORD kIdleWarningMilliseconds = 180000;
constexpr DWORD kIdleLockMilliseconds = 300000;
bool g_idleWarningShown = false;
}

static HttpServer g_httpServer;

void UnlockKiosk()
{
    if (g_kioskState == KioskState::Unlocked)
        return;

    g_kioskState = KioskState::Unlocked;
    g_idleWarningShown = false;
    DispatchIdleWarningCleared();
    OutputDebugStringW(L"[KIOSK] Estado cambiado a UNLOCKED\n");

    ShowWindow(g_hwnd, SW_HIDE);

    CreateSessionWindow(reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE)));
}

void LockKiosk()
{
    if (g_kioskState == KioskState::Locked)
        return;

    g_kioskState = KioskState::Locked;
    g_idleWarningShown = false;
    DispatchIdleWarningCleared();
    OutputDebugStringW(L"[KIOSK] Estado cambiado a LOCKED\n");

    DestroySessionWindow();

    ShowWindow(g_hwnd, SW_SHOW);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
    SetForegroundWindow(g_hwnd);
}

void CloseKiosk()
{
    DestroySessionWindow();
    if (g_hwnd != nullptr)
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
}

static void ConfigureFullscreenWindow(HWND hwnd, bool showWindow)
{
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return;

    const RECT& bounds = monitorInfo.rcMonitor;

    SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_FRAMECHANGED | (showWindow ? SWP_SHOWWINDOW : 0)
    );
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_KIOSK_UNLOCK:
            UnlockKiosk();
            return 0;

        case WM_KIOSK_LOCK:
            LockKiosk();
            return 0;

        case WM_KIOSK_CLOSE:
            CloseKiosk();
            return 0;

        case WM_KIOSK_CENTRAL_RESPONSE:
            DispatchCentralApiResponse(reinterpret_cast<std::wstring*>(lParam));
            return 0;

        case WM_KIOSK_SERVER_IP_RECEIVED:
            DispatchServerIpReceived();
            return 0;

        case WM_TIMER:
            if (wParam == kKioskIdleTimerId)
            {
                if (g_kioskState != KioskState::Unlocked)
                {
                    if (g_idleWarningShown)
                    {
                        g_idleWarningShown = false;
                        DispatchIdleWarningCleared();
                    }
                    return 0;
                }

                LASTINPUTINFO lastInput{};
                lastInput.cbSize = sizeof(lastInput);
                if (!GetLastInputInfo(&lastInput))
                    return 0;

                // GetLastInputInfo puede devolver una marca de tiempo no monotónica.
                const LONG idleDifference = static_cast<LONG>(GetTickCount() - lastInput.dwTime);
                const DWORD idleMilliseconds = idleDifference > 0
                    ? static_cast<DWORD>(idleDifference)
                    : 0;

                if (idleMilliseconds >= kIdleLockMilliseconds)
                {
                    g_idleWarningShown = false;
                    DispatchIdleWarningCleared();
                    LockKiosk();
                }
                else if (idleMilliseconds >= kIdleWarningMilliseconds)
                {
                    if (!g_idleWarningShown)
                    {
                        g_idleWarningShown = true;
                        DispatchIdleWarning();
                    }
                }
                else if (g_idleWarningShown)
                {
                    g_idleWarningShown = false;
                    DispatchIdleWarningCleared();
                }
                return 0;
            }
            break;

        case WM_DISPLAYCHANGE:
            ConfigureFullscreenWindow(hwnd, false);
            ResizeWebView();
            return 0;

        case WM_DPICHANGED:
            ConfigureFullscreenWindow(hwnd, false);
            ResizeWebView();
            return 0;

        case WM_SIZE:
            ResizeWebView();
            return 0;

        case WM_DESTROY:
            g_httpServer.Stop();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Qt 6 comparte el bucle de mensajes Win32 para mantener operativos
    // la ventana nativa del kiosco y las callbacks de WebView2.
    int qtArgc = 1;
    char appName[] = "DesktopKiosk";
    char* qtArgv[] = { appName, nullptr };
    QApplication qtApp(qtArgc, qtArgv);
    qtApp.setApplicationName(QStringLiteral("DesktopKiosk"));
    QApplication::setQuitOnLastWindowClosed(false);

    const wchar_t CLASS_NAME[] = L"DesktopKioskWindow";

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = CLASS_NAME;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (!RegisterClassW(&windowClass))
        return 1;

    if (!RegisterSessionWindowClass(hInstance))
    {
        MessageBoxW(
            nullptr,
            L"No se pudo registrar la clase de ventana de sesión.",
            L"DesktopKiosk",
            MB_ICONERROR
        );
        return 1;
    }

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_TOPMOST,
        CLASS_NAME,
        L"DesktopKiosk",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hwnd)
    {
        MessageBoxW(nullptr, L"No se pudo crear la ventana.", L"DesktopKiosk", MB_ICONERROR);
        return 1;
    }

    // Configurar el firewall antes de mostrar el kiosco a pantalla completa.
    if (!EnsureKioskFirewallRule())
    {
        MessageBoxW(
            nullptr,
            L"No se pudo autorizar DesktopKiosk en el Firewall de Windows. Se requieren permisos para permitir TCP 7085 en redes privadas y públicas.",
            L"DesktopKiosk",
            MB_ICONERROR | MB_OK
        );
        DestroyWindow(g_hwnd);
        return 1;
    }

    ConfigureFullscreenWindow(g_hwnd, true);
    UpdateWindow(g_hwnd);

    InitializeWebView2(g_hwnd);
    SetTimer(g_hwnd, kKioskIdleTimerId, 1000, nullptr);

    if (!g_httpServer.Start(7085, [](const std::string& action)
    {
        if (action == "unlock")
            PostMessageW(g_hwnd, WM_KIOSK_UNLOCK, 0, 0);
        else if (action == "lock")
            PostMessageW(g_hwnd, WM_KIOSK_LOCK, 0, 0);
        else if (action == "close")
            PostMessageW(g_hwnd, WM_KIOSK_CLOSE, 0, 0);
        else if (action == "server_ip_received")
            PostMessageW(g_hwnd, WM_KIOSK_SERVER_IP_RECEIVED, 0, 0);
    }))
    {
        OutputDebugStringW(L"[HTTP] Servidor REST no iniciado.\n");
    }

    return qtApp.exec();
}
