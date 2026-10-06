#include "WebViewManager.h"
#include "KioskMessages.h"
#include "SystemUtils.h"

#include <wrl.h>
#include <WebView2.h>
#include <iostream>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static HWND g_mainHwnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller = nullptr;
static ComPtr<ICoreWebView2> g_webview = nullptr;

void ResizeWebView()
{
    if (g_controller && g_mainHwnd)
    {
        RECT bounds{};
        GetClientRect(g_mainHwnd, &bounds);
        g_controller->put_Bounds(bounds);
    }
}

static bool ExtractAction(const std::wstring& json, std::wstring& action)
{
    const std::wstring key = L"\"action\"";
    const size_t keyPos = json.find(key);
    if (keyPos == std::wstring::npos)
        return false;

    size_t colon = json.find(L':', keyPos + key.size());
    if (colon == std::wstring::npos)
        return false;

    size_t value = colon + 1;
    while (value < json.size() && iswspace(json[value]))
        ++value;

    if (value >= json.size() || json[value] != L'\"')
        return false;

    ++value;
    const size_t end = json.find(L'\"', value);
    if (end == std::wstring::npos)
        return false;

    action = json.substr(value, end - value);
    return action == L"unlock" || action == L"lock" || action == L"close";
}

static void HandleWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args)
{
    wchar_t* source = nullptr;
    if (FAILED(args->get_Source(&source)))
        return;

    const std::wstring expectedSource = GetWebPageUrl();
    const bool trustedSource = source != nullptr && std::wstring(source) == expectedSource;
    CoTaskMemFree(source);

    if (!trustedSource)
    {
        OutputDebugStringW(L"[WEBVIEW2] Mensaje rechazado: origen no autorizado.\n");
        return;
    }

    wchar_t* message = nullptr;
    if (FAILED(args->get_WebMessageAsJson(&message)))
        return;

    const std::wstring json(message);
    CoTaskMemFree(message);

    std::wstring action;
    if (!ExtractAction(json, action))
    {
        OutputDebugStringW(L"[WEBVIEW2] Mensaje rechazado: JSON/action invalido.\n");
        return;
    }

    if (action == L"unlock")
        PostMessageW(g_mainHwnd, WM_KIOSK_UNLOCK, 0, 0);
    else if (action == L"lock")
        PostMessageW(g_mainHwnd, WM_KIOSK_LOCK, 0, 0);
    else if (action == L"close")
        PostMessageW(g_mainHwnd, WM_KIOSK_CLOSE, 0, 0);
}

void InitializeWebView2(HWND hwnd)
{
    g_mainHwnd = hwnd;

    HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        nullptr,
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
            {
                if (FAILED(result))
                {
                    MessageBoxW(hwnd, L"No se pudo inicializar WebView2.", L"DesktopKiosk", MB_ICONERROR);
                    return result;
                }

                return environment->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (FAILED(result))
                            {
                                MessageBoxW(hwnd, L"No se pudo crear el controlador de WebView2.", L"DesktopKiosk", MB_ICONERROR);
                                return result;
                            }

                            g_controller = controller;

                            HRESULT hr = controller->get_CoreWebView2(&g_webview);
                            if (FAILED(hr))
                                return hr;

                            ResizeWebView();

                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                                    {
                                        HandleWebMessage(args);
                                        return S_OK;
                                    })
                                .Get(),
                                nullptr);

                            std::wstring url = GetWebPageUrl();
                            if (url.empty())
                            {
                                MessageBoxW(hwnd, L"No se pudo localizar web/index.html.", L"DesktopKiosk", MB_ICONERROR);
                                return E_FAIL;
                            }

                            return g_webview->Navigate(url.c_str());
                        })
                    .Get());
            })
        .Get());

    if (FAILED(result))
    {
        MessageBoxW(hwnd, L"CreateCoreWebView2EnvironmentWithOptions fallo.", L"DesktopKiosk", MB_ICONERROR);
    }
}
