#include "SessionWindow.h"
#include "SystemUtils.h"

#include <chrono>
#include <string>
#include <cstdio>

// Conexión con funciones/variables externas de main.cpp
extern HWND g_hwnd;
void LockKiosk();

// ============================================================
// Variables privadas del módulo SessionWindow
// ============================================================
static HWND g_sessionWindow = nullptr;

static HWND g_sessionTimeLabel = nullptr;
static HWND g_sessionPcLabel = nullptr;
static HWND g_sessionIpLabel = nullptr;
static HWND g_sessionUserLabel = nullptr;

static HWND g_moveButton = nullptr;
static bool g_moveRight = true;

static HFONT g_sessionFont = nullptr;
static HFONT g_sessionTitleFont = nullptr;
static HFONT g_sessionTimeFont = nullptr;

static std::chrono::steady_clock::time_point g_sessionStart;

// ============================================================
// Actualizar tiempo transcurrido
// ============================================================
void UpdateSessionTime()
{
    if (!g_sessionTimeLabel) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_sessionStart).count();

    int hours   = static_cast<int>(elapsed / 3600);
    int minutes = static_cast<int>((elapsed % 3600) / 60);
    int seconds = static_cast<int>(elapsed % 60);

    wchar_t buffer[64];
    swprintf_s(buffer, L"%02d:%02d:%02d", hours, minutes, seconds);

    SetWindowTextW(g_sessionTimeLabel, buffer);
}

// ============================================================
// Registrar clase de ventana de sesión
// ============================================================
bool RegisterSessionWindowClass(HINSTANCE hInstance)
{
    static bool registered = false;

    if (registered)
        return true;

    const wchar_t SESSION_CLASS_NAME[] = L"DesktopKioskSessionWindow";

    WNDCLASSW sessionClass{};
    sessionClass.lpfnWndProc   = SessionWindowProc;
    sessionClass.hInstance     = hInstance;
    sessionClass.lpszClassName = SESSION_CLASS_NAME;
    sessionClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassW(&sessionClass) != 0)
    {
        registered = true;
        return true;
    }

    if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
        GetClassInfoW(hInstance, SESSION_CLASS_NAME, &sessionClass) != 0)
    {
        registered = true;
        return true;
    }

    return false;
}

// ============================================================
// Window Procedure de la sesión
// ============================================================
LRESULT CALLBACK SessionWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_TIMER:
        {
            if (wParam == 1) { UpdateSessionTime(); }
            return 0;
        }

        case WM_COMMAND:
        {
            int buttonId = LOWORD(wParam);

            // Volver al Kiosk / Bloquear
            if (buttonId == 1001)
            {
                LockKiosk();
                return 0;
            }

            // Mover ventana a la derecha/izquierda
            if (buttonId == 1002)
            {
                RECT rect;
                GetWindowRect(hwnd, &rect);

                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int windowWidth = rect.right - rect.left;
                int newX;

                if (g_moveRight)
                {
                    newX = rect.left + 50;
                    int maxX = screenWidth - windowWidth;
                    if (newX > maxX) newX = maxX;
                }
                else
                {
                    newX = rect.left - 50;
                    if (newX < 0) newX = 0;
                }

                SetWindowPos(hwnd, nullptr, newX, rect.top, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

                g_moveRight = !g_moveRight;
                SetWindowTextW(g_moveButton, g_moveRight ? L"Mover >" : L"Mover <");

                return 0;
            }

            break;
        }

        case WM_CLOSE:
        {
            LockKiosk();
            return 0;
        }

        case WM_DESTROY:
        {
            KillTimer(hwnd, 1);

            if (g_sessionFont) { DeleteObject(g_sessionFont); g_sessionFont = nullptr; }
            if (g_sessionTitleFont) { DeleteObject(g_sessionTitleFont); g_sessionTitleFont = nullptr; }
            if (g_sessionTimeFont) { DeleteObject(g_sessionTimeFont); g_sessionTimeFont = nullptr; }

            g_sessionTimeLabel = nullptr;
            g_sessionPcLabel   = nullptr;
            g_sessionIpLabel   = nullptr;
            g_sessionUserLabel = nullptr;
            g_moveButton       = nullptr;
            g_sessionWindow    = nullptr;

            return 0;
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ============================================================
// Crear ventana de sesión
// ============================================================
void CreateSessionWindow(HINSTANCE hInstance)
{
    g_sessionStart = std::chrono::steady_clock::now();

    const wchar_t SESSION_CLASS_NAME[] = L"DesktopKioskSessionWindow";

    g_sessionWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        SESSION_CLASS_NAME,
        L"Sesión activa",
        WS_POPUP | WS_BORDER,
        100, 100, 420, 300,
        g_hwnd,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_sessionWindow) return;

    // Fuentes
    g_sessionFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_sessionTitleFont = CreateFontW(24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_sessionTimeFont = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

    // Título
    HWND titleLabel = CreateWindowW(L"STATIC", L"SESIÓN ACTIVA", WS_VISIBLE | WS_CHILD,
        25, 20, 300, 35, g_sessionWindow, nullptr, hInstance, nullptr);
    SendMessageW(titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_sessionTitleFont), TRUE);

    // Usuario
    std::wstring userText = L"Usuario: " + GetUserNameString();
    g_sessionUserLabel = CreateWindowW(L"STATIC", userText.c_str(), WS_VISIBLE | WS_CHILD,
        25, 70, 360, 25, g_sessionWindow, nullptr, hInstance, nullptr);
    SendMessageW(g_sessionUserLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_sessionFont), TRUE);

    // PC
    std::wstring pcText = L"PC: " + GetComputerNameString();
    g_sessionPcLabel = CreateWindowW(L"STATIC", pcText.c_str(), WS_VISIBLE | WS_CHILD,
        25, 100, 360, 25, g_sessionWindow, nullptr, hInstance, nullptr);
    SendMessageW(g_sessionPcLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_sessionFont), TRUE);

    // IP
    std::wstring ipText = L"IP: " + GetIpAddress();
    g_sessionIpLabel = CreateWindowW(L"STATIC", ipText.c_str(), WS_VISIBLE | WS_CHILD,
        25, 130, 360, 25, g_sessionWindow, nullptr, hInstance, nullptr);
    SendMessageW(g_sessionIpLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_sessionFont), TRUE);

    // Contador
    g_sessionTimeLabel = CreateWindowW(L"STATIC", L"00:00:00", WS_VISIBLE | WS_CHILD | SS_CENTER,
        25, 165, 360, 45, g_sessionWindow, nullptr, hInstance, nullptr);
    SendMessageW(g_sessionTimeLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g_sessionTimeFont), TRUE);

    // Botones
    HWND backButton = CreateWindowW(L"BUTTON", L"Volver al Kiosk", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        230, 230, 150, 40, g_sessionWindow, (HMENU)1001, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(backButton, WM_SETFONT, reinterpret_cast<WPARAM>(g_sessionFont), TRUE);

    g_moveButton = CreateWindowW(L"BUTTON", L"Mover >", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        40, 230, 150, 40, g_sessionWindow, (HMENU)1002, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_moveButton, WM_SETFONT, (WPARAM)g_sessionFont, TRUE);

    // Temporizador
    SetTimer(g_sessionWindow, 1, 1000, nullptr);
    UpdateSessionTime();

    // Mostrar ventana
    ShowWindow(g_sessionWindow, SW_SHOW);
    SetForegroundWindow(g_sessionWindow);
}

// ============================================================
// Destruir ventana de sesión
// ============================================================
void DestroySessionWindow()
{
    if (g_sessionWindow)
    {
        DestroyWindow(g_sessionWindow);
        g_sessionWindow = nullptr;
    }
}
