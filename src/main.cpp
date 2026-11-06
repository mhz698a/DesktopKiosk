#include "SystemUtils.h"
#include "SessionWindow.h"
#include "WebViewManager.h"
#include "HttpServer.h"
#include "KioskMessages.h"

#include <windows.h>

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QString>

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

enum class KioskState { Locked, Unlocked };

void LockKiosk();
void CloseKiosk();

KioskState g_kioskState = KioskState::Locked;
HWND g_hwnd = nullptr;

namespace
{
constexpr UINT_PTR kKioskIdleTimerId = 7085;
constexpr UINT_PTR kKioskHeartbeatTimerId = 7086;
constexpr UINT_PTR kWebViewStartupTimerId = 7087;
constexpr UINT kWebViewStartupTimeoutMs = 30000;
constexpr DWORD kIdleWarningMilliseconds = 180000;
constexpr DWORD kIdleLockMilliseconds = 300000;
bool g_idleWarningShown = false;
std::chrono::steady_clock::time_point g_sessionStartedAt{};
HHOOK g_keyboardHook = nullptr;

// WH_KEYBOARD_LL es necesario para poder interceptar atajos que Windows procesa
// antes de entregar mensajes normales a la cola de la ventana. Aunque el hook
// recibe eventos del escritorio, solo bloquea teclas cuando el foreground
// pertenece a este proceso. No se consulta GetAsyncKeyState aquí: Windows aún
// no ha actualizado ese estado cuando ejecuta un hook de bajo nivel.
bool g_leftControlDown = false;
bool g_rightControlDown = false;
bool g_leftShiftDown = false;
bool g_rightShiftDown = false;
bool g_leftAltDown = false;
bool g_rightAltDown = false;

bool IsDesktopKioskForeground() noexcept
{
    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow)
        return false;

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    return foregroundProcessId == GetCurrentProcessId();
}

void UpdateTrackedModifierState(const KBDLLHOOKSTRUCT& key, bool keyDown) noexcept
{
    switch (key.vkCode)
    {
        case VK_LCONTROL:
            g_leftControlDown = keyDown;
            break;
        case VK_RCONTROL:
            g_rightControlDown = keyDown;
            break;
        case VK_CONTROL:
            if ((key.flags & LLKHF_EXTENDED) != 0)
                g_rightControlDown = keyDown;
            else
                g_leftControlDown = keyDown;
            break;

        case VK_LSHIFT:
            g_leftShiftDown = keyDown;
            break;
        case VK_RSHIFT:
            g_rightShiftDown = keyDown;
            break;
        case VK_SHIFT:
            if (MapVirtualKeyW(key.scanCode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT)
                g_rightShiftDown = keyDown;
            else
                g_leftShiftDown = keyDown;
            break;

        case VK_LMENU:
            g_leftAltDown = keyDown;
            break;
        case VK_RMENU:
            g_rightAltDown = keyDown;
            break;
        case VK_MENU:
            if ((key.flags & LLKHF_EXTENDED) != 0)
                g_rightAltDown = keyDown;
            else
                g_leftAltDown = keyDown;
            break;
    }
}

LRESULT CALLBACK KioskKeyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && lParam != 0)
    {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool keyDown =
            wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool keyUp =
            wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        if (keyDown || keyUp)
        {
            // Actualiza el estado propio antes de evaluar el atajo. El hook
            // de bajo nivel se ejecuta antes de que Windows actualice el suyo.
            UpdateTrackedModifierState(*key, keyDown);

            if (IsDesktopKioskForeground())
            {
                // La tecla Windows se bloquea en ambos lados para evitar que
                // abra Inicio o deje un estado de tecla modificado.
                if (key->vkCode == VK_LWIN || key->vkCode == VK_RWIN)
                    return 1;

                const bool controlDown = g_leftControlDown || g_rightControlDown;
                const bool shiftDown = g_leftShiftDown || g_rightShiftDown;
                const bool altDown =
                    g_leftAltDown || g_rightAltDown ||
                    (key->flags & LLKHF_ALTDOWN) != 0;

                // Ctrl+Shift+Esc se deja pasar expresamente; Ctrl+Alt+Supr no
                // coincide con ninguna de las teclas bloqueadas.
                if (key->vkCode == VK_ESCAPE && controlDown &&
                    !shiftDown && !altDown)
                {
                    return 1;
                }

                if (key->vkCode == VK_TAB && altDown)
                    return 1;
            }
        }
    }

    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}
}

static HttpServer g_httpServer;
static int g_startupExitCode = 0;
static bool g_kioskRuntimeStarted = false;

void StartKioskRuntime()
{
    if (g_kioskRuntimeStarted || g_hwnd == nullptr)
        return;

    g_kioskRuntimeStarted = true;
    KillTimer(g_hwnd, kWebViewStartupTimerId);

    if (SetTimer(g_hwnd, kKioskIdleTimerId, 1000, nullptr) == 0)
        OutputDebugStringW(L"[KIOSK] No se pudo iniciar el temporizador de inactividad.\n");
    if (SetTimer(g_hwnd, kKioskHeartbeatTimerId, 1000, nullptr) == 0)
        OutputDebugStringW(L"[HEARTBEAT] No se pudo iniciar el temporizador de heartbeat.\n");

    if (!g_httpServer.Start(7085, [](const std::string& action, const std::string& payload)
    {
        if (action == "unlock")
            PostMessageW(g_hwnd, WM_KIOSK_UNLOCK, 0, 0);
        else if (action == "lock")
            PostMessageW(g_hwnd, WM_KIOSK_LOCK, 0, 0);
        else if (action == "close")
            PostMessageW(g_hwnd, WM_KIOSK_CLOSE, 0, 0);
        else if (action == "server_ip_received")
            PostMessageW(g_hwnd, WM_KIOSK_SERVER_IP_RECEIVED, 0, 0);
        else if (action == "assigned_session")
        {
            auto* assignedUser = new std::wstring(
                QString::fromUtf8(payload.c_str()).toStdWString()
            );
            if (!PostMessageW(
                g_hwnd,
                WM_KIOSK_ASSIGNED_SESSION,
                0,
                reinterpret_cast<LPARAM>(assignedUser)))
            {
                delete assignedUser;
            }
        }
        else if (action == "shutdown_pc")
            PostMessageW(g_hwnd, WM_KIOSK_SHUTDOWN_PC, 0, 0);
        else if (action == "restart_pc")
            PostMessageW(g_hwnd, WM_KIOSK_RESTART_PC, 0, 0);
    }))
    {
        OutputDebugStringW(L"[HTTP] Servidor REST no iniciado.\n");
    }
}

std::wstring GetKioskTimeString()
{
    if (g_kioskState != KioskState::Unlocked)
        return L"00:00:00";

    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - g_sessionStartedAt).count();
    const auto hours = elapsed / 3600;
    const auto minutes = (elapsed % 3600) / 60;
    const auto seconds = elapsed % 60;

    std::wostringstream output;
    output << std::setfill(L'0') << std::setw(2) << hours
           << L":" << std::setw(2) << minutes
           << L":" << std::setw(2) << seconds;
    return output.str();
}

void UnlockKiosk()
{
    if (g_kioskState == KioskState::Unlocked)
        return;

    g_kioskState = KioskState::Unlocked;
    g_sessionStartedAt = std::chrono::steady_clock::now();
    g_idleWarningShown = false;
    DispatchIdleWarningCleared();
    OutputDebugStringW(L"[KIOSK] Estado cambiado a UNLOCKED\n");

    ShowWindow(g_hwnd, SW_HIDE);

    CreateSessionWindow(reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE)));
}

void UnlockKioskForUser(const std::wstring& assignedUser)
{
    if (assignedUser.empty())
        return;

    g_kioskState = KioskState::Unlocked;
    g_sessionStartedAt = std::chrono::steady_clock::now();
    g_idleWarningShown = false;
    DispatchIdleWarningCleared();
    OutputDebugStringW(L"[KIOSK] Sesión asignada desde DesktopKioskCenter.\\n");

    DestroySessionWindow();
    ShowWindow(g_hwnd, SW_HIDE);

    CreateSessionWindow(
        reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(g_hwnd, GWLP_HINSTANCE)),
        assignedUser
    );
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
        PostMessageW(g_hwnd, WM_KIOSK_PROGRAMMATIC_CLOSE, 0, 0);
}

bool RequestWindowsPowerAction(bool restart)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            &token))
    {
        OutputDebugStringW(L"[POWER] No se pudo abrir el token de proceso.\n");
        MessageBoxW(
            g_hwnd,
            L"No se pudo preparar el apagado o reinicio de Windows. Comprueba los privilegios de la cuenta.",
            L"DesktopKiosk",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid))
    {
        CloseHandle(token);
        OutputDebugStringW(L"[POWER] No se encontró el privilegio SE_SHUTDOWN_NAME.\n");
        MessageBoxW(
            g_hwnd,
            L"La cuenta de Windows no tiene disponible el privilegio necesario para apagar o reiniciar el equipo.",
            L"DesktopKiosk",
            MB_OK | MB_ICONERROR
        );
        return false;
    }
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    SetLastError(ERROR_SUCCESS);
    const BOOL privilegeAdjusted = AdjustTokenPrivileges(
        token,
        FALSE,
        &privileges,
        sizeof(TOKEN_PRIVILEGES),
        nullptr,
        nullptr
    );
    const DWORD privilegeError = GetLastError();
    CloseHandle(token);

    if (!privilegeAdjusted || privilegeError != ERROR_SUCCESS)
    {
        OutputDebugStringW(L"[POWER] No se pudo habilitar SE_SHUTDOWN_NAME.\n");
        MessageBoxW(
            g_hwnd,
            L"La cuenta de Windows no tiene permiso para apagar o reiniciar este equipo.",
            L"DesktopKiosk",
            MB_OK | MB_ICONERROR
        );
        return false;
    }

    const UINT flags = (restart ? EWX_REBOOT : EWX_POWEROFF) | EWX_FORCEIFHUNG;
    const DWORD reason = SHTDN_REASON_MAJOR_APPLICATION
        | SHTDN_REASON_MINOR_OTHER
        | SHTDN_REASON_FLAG_PLANNED;
    if (!ExitWindowsEx(flags, reason))
    {
        const DWORD errorCode = GetLastError();
        wchar_t message[256]{};
        swprintf_s(
            message,
            L"Windows no pudo %ls el equipo (error %lu).",
            restart ? L"reiniciar" : L"apagar",
            errorCode
        );
        OutputDebugStringW(message);
        OutputDebugStringW(L"\n");
        MessageBoxW(g_hwnd, message, L"DesktopKiosk", MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
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
        case WM_KIOSK_WEBVIEW_READY:
            StartKioskRuntime();
            return 0;

        case WM_KIOSK_STARTUP_FAILED:
        {
            KillTimer(hwnd, kWebViewStartupTimerId);
            g_startupExitCode = 1;

            auto* failureMessage = reinterpret_cast<std::wstring*>(lParam);
            const wchar_t* displayedMessage = failureMessage != nullptr
                ? failureMessage->c_str()
                : L"DesktopKiosk no pudo inicializar WebView2. La aplicación se cerrará.";

            MessageBoxW(
                hwnd,
                displayedMessage,
                L"Error de inicio - DesktopKiosk",
                MB_OK | MB_ICONERROR
            );
            delete failureMessage;

            if (IsWindow(hwnd))
                DestroyWindow(hwnd);
            return 0;
        }

        case WM_KIOSK_UNLOCK:
            UnlockKiosk();
            return 0;

        case WM_KIOSK_ASSIGNED_SESSION:
        {
            auto* assignedUser = reinterpret_cast<std::wstring*>(lParam);
            if (assignedUser != nullptr)
            {
                UnlockKioskForUser(*assignedUser);
                delete assignedUser;
            }
            return 0;
        }

        case WM_KIOSK_LOCK:
            LockKiosk();
            return 0;

        case WM_KIOSK_CLOSE:
            CloseKiosk();
            return 0;

        case WM_KIOSK_PROGRAMMATIC_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_KIOSK_SHUTDOWN_PC:
            RequestWindowsPowerAction(false);
            return 0;

        case WM_KIOSK_RESTART_PC:
            RequestWindowsPowerAction(true);
            return 0;

        case WM_CLOSE:
            // Los cierres nativos no autorizados (incluidos los de respaldo
            // para Alt+F4) no deben sacar al usuario del modo kiosco.
            return 0;

        case WM_KIOSK_CENTRAL_RESPONSE:
            DispatchCentralApiResponse(reinterpret_cast<std::wstring*>(lParam));
            return 0;

        case WM_KIOSK_SERVER_IP_RECEIVED:
            DispatchServerIpReceived();
            return 0;

        case WM_TIMER:
            if (wParam == kWebViewStartupTimerId)
            {
                KillTimer(hwnd, kWebViewStartupTimerId);
                g_startupExitCode = 1;
                MessageBoxW(
                    hwnd,
                    L"WebView2 no terminó de inicializarse dentro de 30 segundos. "
                    L"Comprueba que Microsoft Edge WebView2 Runtime esté instalado y funcionando. "
                    L"DesktopKiosk se cerrará.",
                    L"Tiempo de espera agotado - DesktopKiosk",
                    MB_OK | MB_ICONERROR
                );
                DestroyWindow(hwnd);
                return 0;
            }

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

            if (wParam == kKioskHeartbeatTimerId)
            {
                const int status = g_kioskState == KioskState::Locked
                    ? 1
                    : (g_idleWarningShown ? 3 : 2);
                SendKioskHeartbeat(status, GetKioskTimeString());
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
            KillTimer(hwnd, kWebViewStartupTimerId);
            KillTimer(hwnd, kKioskIdleTimerId);
            KillTimer(hwnd, kKioskHeartbeatTimerId);
            if (g_keyboardHook)
            {
                UnhookWindowsHookEx(g_keyboardHook);
                g_keyboardHook = nullptr;
            }
            g_hwnd = nullptr;
            g_httpServer.Stop();
            PostQuitMessage(g_startupExitCode);
            return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Bloquea una segunda ejecución antes de inicializar Qt o crear ventanas.
    // El objeto Local se comparte entre procesos de la misma sesión de Windows.
    constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\DesktopKiosk.SingleInstance";
    SetLastError(ERROR_SUCCESS);
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
    if (singleInstanceMutex == nullptr)
    {
        const DWORD errorCode = GetLastError();
        wchar_t errorMessage[256]{};
        swprintf_s(
            errorMessage,
            L"No se pudo comprobar si DesktopKiosk ya está ejecutándose (error %lu).",
            errorCode
        );
        MessageBoxW(nullptr, errorMessage, L"DesktopKiosk", MB_OK | MB_ICONERROR);
        return 1;
    }

    const DWORD mutexError = GetLastError();
    if (mutexError == ERROR_ALREADY_EXISTS)
    {
        // La instancia principal conserva el handle; esta ejecución no debe
        // crear otra ventana ni intentar abrir de nuevo el puerto HTTP 7085.
        CloseHandle(singleInstanceMutex);
        return 0;
    }

    // Qt 6 comparte el bucle de mensajes Win32 para mantener operativos
    // la ventana nativa del kiosco y las callbacks de WebView2.
    int qtArgc = 1;
    char appName[] = "DesktopKiosk";
    char* qtArgv[] = { appName, nullptr };
    std::unique_ptr<QApplication> qtApp;
    try
    {
        qtApp = std::make_unique<QApplication>(qtArgc, qtArgv);
    }
    catch (...)
    {
        MessageBoxW(
            nullptr,
            L"Qt no pudo inicializar la aplicación gráfica. DesktopKiosk se cerrará.",
            L"Error de inicialización de Qt",
            MB_OK | MB_ICONERROR
        );
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (QApplication::instance() != qtApp.get()
        || QGuiApplication::platformName().isEmpty()
        || QGuiApplication::primaryScreen() == nullptr)
    {
        MessageBoxW(
            nullptr,
            L"Qt no pudo inicializar correctamente la plataforma gráfica o detectar una pantalla. "
            L"DesktopKiosk se cerrará.",
            L"Error de inicialización de Qt",
            MB_OK | MB_ICONERROR
        );
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    qtApp->setApplicationName(QStringLiteral("DesktopKiosk"));
    QApplication::setQuitOnLastWindowClosed(false);

    const wchar_t CLASS_NAME[] = L"DesktopKioskWindow";

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = CLASS_NAME;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (!RegisterClassW(&windowClass))
    {
        const DWORD errorCode = GetLastError();
        wchar_t message[256]{};
        swprintf_s(
            message,
            L"No se pudo registrar la ventana de DesktopKiosk (error %lu).",
            errorCode
        );
        MessageBoxW(nullptr, message, L"Error de inicio - DesktopKiosk", MB_OK | MB_ICONERROR);
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (!RegisterSessionWindowClass(hInstance))
    {
        MessageBoxW(
            nullptr,
            L"Qt no pudo preparar la ventana de sesión. DesktopKiosk se cerrará.",
            L"Error de inicialización de Qt",
            MB_OK | MB_ICONERROR
        );
        CloseHandle(singleInstanceMutex);
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
        MessageBoxW(nullptr, L"No se pudo crear la ventana principal. DesktopKiosk se cerrará.", L"DesktopKiosk", MB_OK | MB_ICONERROR);
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    ConfigureFullscreenWindow(g_hwnd, true);
    UpdateWindow(g_hwnd);

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KioskKeyboardHookProc,
        GetModuleHandleW(nullptr),
        0
    );
    if (!g_keyboardHook)
    {
        const DWORD errorCode = GetLastError();
        wchar_t errorMessage[256]{};
        swprintf_s(
            errorMessage,
            L"No se pudo activar el bloqueo de atajos de teclado (error %lu). "
            L"DesktopKiosk no puede iniciar en modo kiosco.",
            errorCode
        );
        MessageBoxW(g_hwnd, errorMessage, L"DesktopKiosk", MB_OK | MB_ICONERROR);
        DestroyWindow(g_hwnd);
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    if (SetTimer(g_hwnd, kWebViewStartupTimerId, kWebViewStartupTimeoutMs, nullptr) == 0)
    {
        MessageBoxW(
            g_hwnd,
            L"No se pudo iniciar el temporizador de seguridad de arranque. DesktopKiosk se cerrará.",
            L"Error de inicio - DesktopKiosk",
            MB_OK | MB_ICONERROR
        );
        DestroyWindow(g_hwnd);
        ShutdownWebView2();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    // La aplicación no activa el kiosco, el heartbeat ni la API hasta que
    // WebView2 confirme que la página inicial terminó de cargar correctamente.
    InitializeWebView2(g_hwnd);

    const int eventLoopExitCode = qtApp->exec();
    ShutdownWebView2();
    CloseHandle(singleInstanceMutex);

    return g_startupExitCode != 0 ? g_startupExitCode : eventLoopExitCode;
}
