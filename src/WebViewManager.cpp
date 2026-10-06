#include "WebViewManager.h"
#include "SystemUtils.h"

#include <wrl.h>
#include <WebView2.h>
#include <iostream>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

// Conexión con las funciones de control de estado en main.cpp
extern void UnlockKiosk();
extern void LockKiosk();
extern void CloseKiosk();

// =============================================
// Variables privadas del módulo WebView2
// =============================================
static HWND g_mainHwnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller = nullptr;
static ComPtr<ICoreWebView2> g_webview = nullptr;

// =============================================
// Redimensionar WebView2
// =============================================
void ResizeWebView()
{
    if (g_controller && g_mainHwnd)
    {
        RECT bounds;
        GetClientRect(g_mainHwnd, &bounds);
        g_controller->put_Bounds(bounds);
    }
}

// =============================================
// Procesar mensajes enviados desde JavaScript
// =============================================
static void HandleWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args)
{
    wchar_t* message = nullptr;
    HRESULT result = args->get_WebMessageAsJson(&message);

    if (FAILED(result)) return;

    std::wstring json(message);
    CoTaskMemFree(message);

    std::wcout << L"[WEBVIEW2] Mensaje recibido: " << json << std::endl;

    if (json.find(L"\"unlock\"") != std::wstring::npos)
    {
        UnlockKiosk();
    }
    else if (json.find(L"\"lock\"") != std::wstring::npos)
    {
        LockKiosk();
    }
    else if (json.find(L"\"close\"") != std::wstring::npos)
    {
        CloseKiosk();
    }
}

// =============================================
// Inicializar WebView2
// =============================================
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

                return 
                
                // -------------------------------------------
                // Crear el controlador asociado a nuestra ventana
                // -------------------------------------------

                environment->CreateCoreWebView2Controller(
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
                            if (FAILED(hr)) return hr;

                            // Ajustar tamaño inicial
                            ResizeWebView();

                            // Registrar receptor de mensajes JavaScript
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                                    {
                                        HandleWebMessage(args);
                                        return S_OK;
                                    })
                                .Get(),
                                nullptr);

                            // Cargar HTML
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
        MessageBoxW(hwnd, L"CreateCoreWebView2EnvironmentWithOptions falló.", L"DesktopKiosk", MB_ICONERROR);
    }
}