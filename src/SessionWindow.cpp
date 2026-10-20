#include "SessionWindow.h"
#include "SystemUtils.h"

#include <algorithm>
#include <chrono>
#include <string>

extern HWND g_hwnd;
void LockKiosk();

namespace
{
constexpr wchar_t kSessionClassName[] = L"DesktopKioskSessionWindow";
constexpr wchar_t kToggleClassName[] = L"DesktopKioskSessionToggleWindow";

constexpr int kToggleSize = 58;
constexpr int kPanelWidth = 350;
constexpr int kPanelHeight = 280;
constexpr int kScreenMargin = 18;
constexpr UINT_PTR kSessionTimerId = 1;
constexpr int kCloseSessionButtonId = 1001;

constexpr int kHeaderLabelId = 2001;
constexpr int kDetailsLabelId = 2002;
constexpr int kPcLabelId = 2003;
constexpr int kUserLabelId = 2004;
constexpr int kIpLabelId = 2005;
constexpr int kTimeLabelId = 2006;

HWND g_sessionWindow = nullptr;
HWND g_toggleWindow = nullptr;

HWND g_sessionTimeLabel = nullptr;
HWND g_sessionPcLabel = nullptr;
HWND g_sessionIpLabel = nullptr;
HWND g_sessionUserLabel = nullptr;
HWND g_closeSessionButton = nullptr;

HFONT g_sessionFont = nullptr;
HFONT g_sessionTitleFont = nullptr;
HFONT g_sessionTimeFont = nullptr;
HBRUSH g_sessionBackgroundBrush = nullptr;

WNDPROC g_closeButtonOriginalProc = nullptr;
std::chrono::steady_clock::time_point g_sessionStart;
bool g_toggleHovered = false;
bool g_trackingMouse = false;

LRESULT CALLBACK ToggleWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SessionCloseButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

bool RegisterWindowClass(HINSTANCE hInstance, const wchar_t* className,
                         WNDPROC windowProc, UINT classStyle)
{
    WNDCLASSW windowClass{};
    windowClass.style = classStyle;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassW(&windowClass) != 0)
        return true;

    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    WNDCLASSW existingClass{};
    return GetClassInfoW(hInstance, className, &existingClass) != 0
        && existingClass.lpfnWndProc == windowProc;
}

HWND CreateSessionLabel(HINSTANCE hInstance, HWND parent, const wchar_t* text,
                        int controlId, int x, int y, int width, int height,
                        HFONT font, DWORD extraStyle = SS_LEFT)
{
    HWND label = CreateWindowExW(
        0,
        L"STATIC",
        text,
        WS_CHILD | WS_VISIBLE | extraStyle,
        x, y, width, height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        hInstance,
        nullptr
    );

    if (label != nullptr && font != nullptr)
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    return label;
}

void PositionSessionWindows()
{
    HWND monitorWindow = g_hwnd != nullptr ? g_hwnd : g_toggleWindow;
    HMONITOR monitor = MonitorFromWindow(monitorWindow, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo))
        return;

    const RECT bounds = monitorInfo.rcMonitor;
    const int monitorWidth = bounds.right - bounds.left;
    const int monitorHeight = bounds.bottom - bounds.top;

    const int toggleSize = std::min(kToggleSize, std::max(32, monitorWidth - 2 * kScreenMargin));
    const int toggleX = bounds.right - toggleSize - kScreenMargin;
    const int toggleY = bounds.top + kScreenMargin;

    if (g_toggleWindow != nullptr)
    {
        SetWindowPos(
            g_toggleWindow,
            HWND_TOPMOST,
            toggleX,
            toggleY,
            toggleSize,
            toggleSize,
            SWP_NOACTIVATE | SWP_SHOWWINDOW
        );

        HRGN region = CreateEllipticRgn(0, 0, toggleSize, toggleSize);
        if (region != nullptr)
        {
            if (!SetWindowRgn(g_toggleWindow, region, TRUE))
                DeleteObject(region);
        }
    }

    if (g_sessionWindow != nullptr)
    {
        const int panelWidth = std::min(kPanelWidth, std::max(240, monitorWidth - 2 * kScreenMargin));
        const int panelHeight = std::min(kPanelHeight, std::max(220, monitorHeight - 2 * kScreenMargin));

        int panelX = toggleX + toggleSize - panelWidth;
        int panelY = toggleY + toggleSize + 10;

        if (panelY + panelHeight > bounds.bottom - kScreenMargin)
            panelY = toggleY - panelHeight - 10;

        const int minX = bounds.left + kScreenMargin;
        const int maxX = std::max(minX, bounds.right - panelWidth - kScreenMargin);
        const int minY = bounds.top + kScreenMargin;
        const int maxY = std::max(minY, bounds.bottom - panelHeight - kScreenMargin);

        panelX = std::clamp(panelX, minX, maxX);
        panelY = std::clamp(panelY, minY, maxY);

        SetWindowPos(
            g_sessionWindow,
            HWND_TOPMOST,
            panelX,
            panelY,
            panelWidth,
            panelHeight,
            SWP_NOACTIVATE
        );
    }
}

void ShowOrHideSessionPanel()
{
    if (g_sessionWindow == nullptr)
        return;

    if (IsWindowVisible(g_sessionWindow))
    {
        ShowWindow(g_sessionWindow, SW_HIDE);
        return;
    }

    PositionSessionWindows();
    ShowWindow(g_sessionWindow, SW_SHOW);
    SetForegroundWindow(g_sessionWindow);

    if (g_closeSessionButton != nullptr)
        SetFocus(g_closeSessionButton);
}
}

// ============================================================
// Actualizar el tiempo transcurrido de la sesión.
// El temporizador sigue funcionando aunque el panel esté oculto.
// ============================================================
void UpdateSessionTime()
{
    if (g_sessionTimeLabel == nullptr)
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - g_sessionStart).count();

    const long long hours = elapsed / 3600;
    const long long minutes = (elapsed % 3600) / 60;
    const long long seconds = elapsed % 60;

    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%02lld:%02lld:%02lld", hours, minutes, seconds);
    SetWindowTextW(g_sessionTimeLabel, buffer);
}

// ============================================================
// Registrar las clases de las dos ventanas flotantes.
// ============================================================
bool RegisterSessionWindowClass(HINSTANCE hInstance)
{
    if (!RegisterWindowClass(
            hInstance, kSessionClassName, SessionWindowProc, CS_DROPSHADOW))
        return false;

    if (!RegisterWindowClass(
            hInstance, kToggleClassName, ToggleWindowProc, CS_HREDRAW | CS_VREDRAW))
        return false;

    return true;
}

// ============================================================
// Ventana del botón circular persistente.
// ============================================================
static LRESULT CALLBACK ToggleWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC hdc = BeginPaint(hwnd, &paint);

            RECT rect{};
            GetClientRect(hwnd, &rect);

            const COLORREF fillColor = g_toggleHovered
                ? RGB(59, 130, 246)
                : RGB(37, 99, 235);

            HBRUSH background = CreateSolidBrush(fillColor);
            HPEN border = CreatePen(PS_SOLID, 1, fillColor);
            HGDIOBJ oldBrush = SelectObject(hdc, background);
            HGDIOBJ oldPen = SelectObject(hdc, border);

            Ellipse(hdc, rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1);

            const int centerX = (rect.left + rect.right) / 2;
            const int centerY = (rect.top + rect.bottom) / 2;
            const int radius = std::max(10, std::min(rect.right - rect.left, rect.bottom - rect.top) / 4);

            HPEN clockPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            SelectObject(hdc, clockPen);

            Ellipse(hdc, centerX - radius, centerY - radius,
                    centerX + radius, centerY + radius);
            MoveToEx(hdc, centerX, centerY, nullptr);
            LineTo(hdc, centerX, centerY - radius / 2);
            MoveToEx(hdc, centerX, centerY, nullptr);
            LineTo(hdc, centerX + radius / 2, centerY + radius / 4);

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(clockPen);
            DeleteObject(border);
            DeleteObject(background);

            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (!g_trackingMouse)
            {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                g_trackingMouse = TrackMouseEvent(&track) != FALSE;
            }

            if (!g_toggleHovered)
            {
                g_toggleHovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_trackingMouse = false;
            g_toggleHovered = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONUP:
            ShowOrHideSessionPanel();
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            PositionSessionWindows();
            return 0;

        case WM_NCDESTROY:
            if (hwnd == g_toggleWindow)
                g_toggleWindow = nullptr;
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ============================================================
// Ventana principal del panel emergente de sesión.
// ============================================================
LRESULT CALLBACK SessionWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_TIMER:
            if (wParam == kSessionTimerId)
                UpdateSessionTime();
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == kCloseSessionButtonId &&
                HIWORD(wParam) == BN_CLICKED)
            {
                LockKiosk();
                return 0;
            }
            break;

        case WM_ERASEBKGND:
        {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            HBRUSH brush = g_sessionBackgroundBrush != nullptr
                ? g_sessionBackgroundBrush
                : GetSysColorBrush(COLOR_WINDOW);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, brush);
            return 1;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            const int controlId = GetDlgCtrlID(control);

            COLORREF textColor = RGB(229, 231, 235);
            if (controlId == kHeaderLabelId)
                textColor = RGB(255, 255, 255);
            else if (controlId == kDetailsLabelId)
                textColor = RGB(147, 197, 253);
            else if (controlId == kTimeLabelId)
                textColor = RGB(96, 165, 250);
            else if (controlId == kUserLabelId || controlId == kIpLabelId)
                textColor = RGB(203, 213, 225);

            SetTextColor(hdc, textColor);
            SetBkColor(hdc, RGB(24, 29, 40));
            SetBkMode(hdc, OPAQUE);

            return reinterpret_cast<LRESULT>(
                g_sessionBackgroundBrush != nullptr
                    ? g_sessionBackgroundBrush
                    : GetSysColorBrush(COLOR_WINDOW));
        }

        case WM_DRAWITEM:
        {
            if (wParam != kCloseSessionButtonId || lParam == 0)
                break;

            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw->CtlType != ODT_BUTTON)
                break;

            const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
            const COLORREF buttonColor = pressed
                ? RGB(29, 78, 216)
                : RGB(37, 99, 235);

            RECT rect = draw->rcItem;
            HBRUSH brush = CreateSolidBrush(buttonColor);
            HPEN pen = CreatePen(PS_SOLID, 1, buttonColor);
            HGDIOBJ oldBrush = SelectObject(draw->hDC, brush);
            HGDIOBJ oldPen = SelectObject(draw->hDC, pen);

            RoundRect(draw->hDC, rect.left, rect.top, rect.right, rect.bottom, 12, 12);

            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, RGB(255, 255, 255));
            HGDIOBJ oldFont = nullptr;
            if (g_sessionFont != nullptr)
                oldFont = SelectObject(draw->hDC, g_sessionFont);

            DrawTextW(draw->hDC, L"Cerrar sesión", -1, &rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            if ((draw->itemState & ODS_FOCUS) != 0)
            {
                InflateRect(&rect, -5, -5);
                DrawFocusRect(draw->hDC, &rect);
            }

            if (oldFont != nullptr)
                SelectObject(draw->hDC, oldFont);
            SelectObject(draw->hDC, oldBrush);
            SelectObject(draw->hDC, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
            return TRUE;
        }

        case WM_ACTIVATE:
            // Un clic fuera del panel lo descarta sin terminar la sesión.
            if (LOWORD(wParam) == WA_INACTIVE && IsWindowVisible(hwnd))
                ShowWindow(hwnd, SW_HIDE);
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;

        case WM_CLOSE:
            // Cerrar el Popup solo lo oculta; el fin de sesión es explícito.
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            PositionSessionWindows();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, kSessionTimerId);

            if (g_sessionFont != nullptr)
            {
                DeleteObject(g_sessionFont);
                g_sessionFont = nullptr;
            }
            if (g_sessionTitleFont != nullptr)
            {
                DeleteObject(g_sessionTitleFont);
                g_sessionTitleFont = nullptr;
            }
            if (g_sessionTimeFont != nullptr)
            {
                DeleteObject(g_sessionTimeFont);
                g_sessionTimeFont = nullptr;
            }
            if (g_sessionBackgroundBrush != nullptr)
            {
                DeleteObject(g_sessionBackgroundBrush);
                g_sessionBackgroundBrush = nullptr;
            }

            g_sessionTimeLabel = nullptr;
            g_sessionPcLabel = nullptr;
            g_sessionIpLabel = nullptr;
            g_sessionUserLabel = nullptr;
            g_closeSessionButton = nullptr;
            g_sessionWindow = nullptr;
            return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static LRESULT CALLBACK SessionCloseButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        ShowWindow(GetParent(hwnd), SW_HIDE);
        return 0;
    }

    WNDPROC originalProc = g_closeButtonOriginalProc;
    if (message == WM_NCDESTROY)
        g_closeButtonOriginalProc = nullptr;

    if (originalProc != nullptr)
        return CallWindowProcW(originalProc, hwnd, message, wParam, lParam);

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ============================================================
// Crear el botón persistente y el panel inicialmente oculto.
// ============================================================
void CreateSessionWindow(HINSTANCE hInstance)
{
    if (g_sessionWindow != nullptr || g_toggleWindow != nullptr)
        return;

    g_sessionStart = std::chrono::steady_clock::now();
    g_toggleHovered = false;
    g_trackingMouse = false;

    g_sessionBackgroundBrush = CreateSolidBrush(RGB(24, 29, 40));

    // No asignamos propietario al botón: debe seguir visible cuando se oculta
    // la ventana principal del kiosco.
    g_toggleWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kToggleClassName,
        L"Mostrar información de sesión",
        WS_POPUP,
        0, 0, kToggleSize, kToggleSize,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (g_toggleWindow == nullptr)
    {
        if (g_sessionBackgroundBrush != nullptr)
        {
            DeleteObject(g_sessionBackgroundBrush);
            g_sessionBackgroundBrush = nullptr;
        }
        return;
    }

    g_sessionWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kSessionClassName,
        L"Sesión activa",
        WS_POPUP | WS_BORDER,
        0, 0, kPanelWidth, kPanelHeight,
        g_toggleWindow,
        nullptr,
        hInstance,
        nullptr
    );

    if (g_sessionWindow == nullptr)
    {
        DestroyWindow(g_toggleWindow);
        g_toggleWindow = nullptr;

        if (g_sessionBackgroundBrush != nullptr)
        {
            DeleteObject(g_sessionBackgroundBrush);
            g_sessionBackgroundBrush = nullptr;
        }
        return;
    }

    g_sessionFont = CreateFontW(
        15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_sessionTitleFont = CreateFontW(
        24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_sessionTimeFont = CreateFontW(
        30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

    CreateSessionLabel(hInstance, g_sessionWindow, L"SESIÓN ACTIVA",
        kHeaderLabelId, 22, 18, 306, 34, g_sessionTitleFont);

    CreateSessionLabel(hInstance, g_sessionWindow, L"DATOS DEL EQUIPO",
        kDetailsLabelId, 24, 58, 302, 20, g_sessionFont);

    g_sessionPcLabel = CreateSessionLabel(
        hInstance, g_sessionWindow,
        (L"Equipo: " + GetComputerNameString()).c_str(),
        kPcLabelId, 24, 82, 302, 22, g_sessionFont);

    g_sessionUserLabel = CreateSessionLabel(
        hInstance, g_sessionWindow,
        (L"Usuario: " + GetUserNameString()).c_str(),
        kUserLabelId, 24, 106, 302, 22, g_sessionFont);

    g_sessionIpLabel = CreateSessionLabel(
        hInstance, g_sessionWindow,
        (L"IP: " + GetIpAddress()).c_str(),
        kIpLabelId, 24, 130, 302, 22, g_sessionFont);

    g_sessionTimeLabel = CreateSessionLabel(
        hInstance, g_sessionWindow, L"00:00:00",
        kTimeLabelId, 22, 156, 306, 42, g_sessionTimeFont, SS_CENTER);

    g_closeSessionButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Cerrar sesión",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        24, 218, 302, 40,
        g_sessionWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseSessionButtonId)),
        hInstance,
        nullptr
    );

    if (g_closeSessionButton != nullptr && g_sessionFont != nullptr)
    {
        SendMessageW(g_closeSessionButton, WM_SETFONT,
                     reinterpret_cast<WPARAM>(g_sessionFont), TRUE);

        SetLastError(0);
        const LONG_PTR originalProc = SetWindowLongPtrW(
            g_closeSessionButton,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(SessionCloseButtonProc));

        if (originalProc != 0)
            g_closeButtonOriginalProc = reinterpret_cast<WNDPROC>(originalProc);
    }

    PositionSessionWindows();

    if (SetTimer(g_sessionWindow, kSessionTimerId, 1000, nullptr) == 0)
        OutputDebugStringW(L"[SESSION] No se pudo crear el temporizador de sesión.\n");

    UpdateSessionTime();

    // Solo se muestra el botón circular. El panel se abre al pulsarlo.
    ShowWindow(g_toggleWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_toggleWindow);
}

// ============================================================
// Destruir todo el componente de sesión.
// ============================================================
void DestroySessionWindow()
{
    if (g_sessionWindow != nullptr)
        DestroyWindow(g_sessionWindow);

    if (g_toggleWindow != nullptr)
        DestroyWindow(g_toggleWindow);

    g_sessionWindow = nullptr;
    g_toggleWindow = nullptr;
    g_sessionTimeLabel = nullptr;
    g_sessionPcLabel = nullptr;
    g_sessionIpLabel = nullptr;
    g_sessionUserLabel = nullptr;
    g_closeSessionButton = nullptr;
}
