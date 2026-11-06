#include <winsock2.h>
#include <ws2tcpip.h>

#include "WebViewManager.h"
#include "KioskMessages.h"
#include "SystemUtils.h"

#include <wrl.h>
#include <WebView2.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{
constexpr DWORD kCentralRequestTimeoutMs = 19000;
constexpr DWORD kHeartbeatRequestTimeoutMs = 800;
constexpr size_t kMaxCentralResponseBytes = 64 * 1024;

HWND g_mainHwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller = nullptr;
ComPtr<ICoreWebView2> g_webview = nullptr;
bool g_pageReady = false;
bool g_comInitialized = false;
unsigned int g_pendingServerIpReceived = 0;
int g_pendingIdleWarningState = 0; // 0 = ninguno, 1 = mostrar, 2 = ocultar
std::atomic<bool> g_heartbeatInFlight{false};

struct HttpResponse
{
    bool received = false;
    int status = 0;
    std::string body;
};

std::string WideToUtf8(const std::wstring& value)
{
    return QString::fromStdWString(value).toUtf8().toStdString();
}

std::wstring ReadServerControlIp()
{
    const DWORD requiredSize = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (requiredSize == 0)
        return {};

    std::wstring appDataPath(requiredSize, L'\0');
    const DWORD copiedSize = GetEnvironmentVariableW(
        L"APPDATA",
        appDataPath.data(),
        requiredSize
    );
    if (copiedSize == 0 || copiedSize >= requiredSize)
        return {};

    appDataPath.resize(copiedSize);
    const std::filesystem::path settingsFile =
        std::filesystem::path(appDataPath) / L"DesktopKiosk" / L"settings.ini";

    wchar_t serverIp[64] = {};
    GetPrivateProfileStringW(
        L"network",
        L"server_control_ip",
        L"",
        serverIp,
        static_cast<DWORD>(sizeof(serverIp) / sizeof(serverIp[0])),
        settingsFile.c_str()
    );

    std::wstring result(serverIp);
    const size_t first = result.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
        return {};
    const size_t last = result.find_last_not_of(L" \t\r\n");
    result = result.substr(first, last - first + 1);

    IN_ADDR parsedAddress{};
    const std::string asciiIp(result.begin(), result.end());
    if (InetPtonA(AF_INET, asciiIp.c_str(), &parsedAddress) != 1)
        return {};

    return result;
}

bool IsAllowedApiPath(const std::string& path)
{
    if (path.rfind("/api/v1/", 0) != 0 || path.find("..") != std::string::npos)
        return false;

    return std::none_of(path.begin(), path.end(), [](unsigned char ch)
    {
        return ch <= 0x20 || ch == 0x7f || ch == '\\' || ch == '\r' || ch == '\n';
    });
}

std::string GetHeaderValue(const std::string& headers, const std::string& name)
{
    size_t start = 0;
    while (start < headers.size())
    {
        const size_t end = headers.find("\r\n", start);
        if (end == std::string::npos)
            break;

        const size_t colon = headers.find(':', start);
        if (colon != std::string::npos && colon < end)
        {
            std::string headerName = headers.substr(start, colon - start);
            std::transform(headerName.begin(), headerName.end(), headerName.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            std::string expectedName = name;
            std::transform(expectedName.begin(), expectedName.end(), expectedName.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            if (headerName == expectedName)
            {
                size_t valueStart = colon + 1;
                while (valueStart < end && (headers[valueStart] == ' ' || headers[valueStart] == '\t'))
                    ++valueStart;
                return headers.substr(valueStart, end - valueStart);
            }
        }

        start = end + 2;
    }

    return {};
}

bool WaitForSocket(
    SOCKET socketHandle,
    bool waitForWrite,
    const std::chrono::steady_clock::time_point& deadline)
{
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero())
        return false;

    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(remaining).count();

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(micros / 1000000);
    timeout.tv_usec = static_cast<long>(micros % 1000000);

    fd_set sockets;
    FD_ZERO(&sockets);
    FD_SET(socketHandle, &sockets);

    return (waitForWrite
        ? select(0, nullptr, &sockets, nullptr, &timeout)
        : select(0, &sockets, nullptr, nullptr, &timeout)) > 0;
}

HttpResponse SendHttpRequest(
    const std::string& serverIp,
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::chrono::steady_clock::time_point& deadline)
{
    HttpResponse result;
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET)
        return result;

    const auto closeSocket = [&socketHandle]()
    {
        if (socketHandle != INVALID_SOCKET)
        {
            closesocket(socketHandle);
            socketHandle = INVALID_SOCKET;
        }
    };

    u_long nonBlocking = 1;
    if (ioctlsocket(socketHandle, FIONBIO, &nonBlocking) == SOCKET_ERROR)
    {
        closeSocket();
        return result;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(7085);
    if (InetPtonA(AF_INET, serverIp.c_str(), &address.sin_addr) != 1)
    {
        closeSocket();
        return result;
    }

    int connectResult = connect(
        socketHandle,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    );

    if (connectResult == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
        {
            closeSocket();
            return result;
        }

        if (!WaitForSocket(socketHandle, true, deadline))
        {
            closeSocket();
            return result;
        }

        int socketError = 0;
        int optionLength = sizeof(socketError);
        if (getsockopt(
            socketHandle,
            SOL_SOCKET,
            SO_ERROR,
            reinterpret_cast<char*>(&socketError),
            &optionLength) == SOCKET_ERROR || socketError != 0)
        {
            closeSocket();
            return result;
        }
    }

    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + serverIp + ":7085\r\n";
    request += "Accept: application/json\r\n";
    request += "Connection: close\r\n";
    request += "X-Kiosk-Name: " + WideToUtf8(GetComputerNameString()) + "\r\n";

    if (method == "POST")
    {
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }

    request += "\r\n";
    request += body;

    size_t sent = 0;
    while (sent < request.size())
    {
        if (!WaitForSocket(socketHandle, true, deadline))
        {
            closeSocket();
            return result;
        }

        const int sendResult = send(
            socketHandle,
            request.data() + sent,
            static_cast<int>(request.size() - sent),
            0
        );

        if (sendResult <= 0)
        {
            closeSocket();
            return result;
        }

        sent += static_cast<size_t>(sendResult);
    }

    std::string response;
    bool hasContentLength = false;
    size_t expectedSize = 0;
    char buffer[4096];

    while (response.size() < kMaxCentralResponseBytes)
    {
        if (!WaitForSocket(socketHandle, false, deadline))
        {
            closeSocket();
            return result;
        }

        const int received = recv(socketHandle, buffer, sizeof(buffer), 0);
        if (received == 0)
            break;
        if (received == SOCKET_ERROR)
        {
            closeSocket();
            return result;
        }

        response.append(buffer, static_cast<size_t>(received));
        if (response.size() > kMaxCentralResponseBytes)
        {
            closeSocket();
            return result;
        }

        const size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd != std::string::npos && !hasContentLength)
        {
            const std::string headers = response.substr(0, headerEnd + 2);
            const std::string lengthText = GetHeaderValue(headers, "Content-Length");
            if (!lengthText.empty())
            {
                try
                {
                    const size_t contentLength = static_cast<size_t>(std::stoull(lengthText));
                    if (contentLength > kMaxCentralResponseBytes - headerEnd - 4)
                    {
                        closeSocket();
                        return result;
                    }

                    expectedSize = headerEnd + 4 + contentLength;
                    hasContentLength = true;
                }
                catch (...)
                {
                    closeSocket();
                    return result;
                }
            }
        }

        if (hasContentLength && response.size() >= expectedSize)
            break;
    }

    closeSocket();

    const size_t headerEnd = response.find("\r\n\r\n");
    const size_t statusLineEnd = response.find("\r\n");
    if (headerEnd == std::string::npos || statusLineEnd == std::string::npos)
        return result;

    std::istringstream statusLine(response.substr(0, statusLineEnd));
    std::string version;
    statusLine >> version >> result.status;
    if (version.rfind("HTTP/", 0) != 0 || result.status < 100 || result.status > 599)
        return HttpResponse{};

    if (hasContentLength && response.size() < expectedSize)
        return HttpResponse{};

    result.body = response.substr(headerEnd + 4);
    result.received = true;
    return result;
}

QJsonObject BuildTransportError(
    const QString& requestId,
    const QString& code,
    const QString& message)
{
    QJsonObject response;
    response.insert(QStringLiteral("type"), QStringLiteral("central-api-response"));
    response.insert(QStringLiteral("requestId"), requestId);
    response.insert(QStringLiteral("transportError"), code);
    response.insert(QStringLiteral("message"), message);
    return response;
}

void PostResponseToWebView(const QJsonObject& apiResponse)
{
    const QByteArray utf8 = QJsonDocument(apiResponse).toJson(QJsonDocument::Compact);
    const std::wstring responseText = QString::fromUtf8(utf8).toStdWString();
    auto* payload = new std::wstring(responseText);

    if (!IsWindow(g_mainHwnd)
        || !PostMessageW(
            g_mainHwnd,
            WM_KIOSK_CENTRAL_RESPONSE,
            0,
            reinterpret_cast<LPARAM>(payload)))
    {
        delete payload;
    }
}

void ProcessCentralApiRequest(
    const QString& requestId,
    const QString& method,
    const QString& path,
    const QJsonObject& payload)
{
    const std::wstring configuredIp = ReadServerControlIp();
    if (configuredIp.empty())
    {
        PostResponseToWebView(BuildTransportError(
            requestId,
            QStringLiteral("central_server_unknown"),
            QStringLiteral("Este cliente no conoce el servidor central, contacte con el administrador de este equipo")
        ));
        return;
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        PostResponseToWebView(BuildTransportError(
            requestId,
            QStringLiteral("central_server_unavailable"),
            QStringLiteral("No se encontro el servidor central, contacte con el administrador de este equipo")
        ));
        return;
    }

    const std::string ip(configuredIp.begin(), configuredIp.end());
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(kCentralRequestTimeoutMs);

    std::string requestBody;
    if (method == "POST")
        requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString();

    const HttpResponse apiResponse = SendHttpRequest(
        ip, method.toStdString(), path.toStdString(), requestBody, deadline
    );
    WSACleanup();

    if (!apiResponse.received)
    {
        PostResponseToWebView(BuildTransportError(
            requestId,
            QStringLiteral("central_server_unavailable"),
            QStringLiteral("No se encontro el servidor central, contacte con el administrador de este equipo")
        ));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(
        QByteArray::fromStdString(apiResponse.body),
        &parseError
    );
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        PostResponseToWebView(BuildTransportError(
            requestId,
            QStringLiteral("invalid_server_response"),
            QStringLiteral("El servidor central devolvió una respuesta inválida.")
        ));
        return;
    }

    QJsonObject response;
    response.insert(QStringLiteral("type"), QStringLiteral("central-api-response"));
    response.insert(QStringLiteral("requestId"), requestId);
    response.insert(QStringLiteral("status"), apiResponse.status);
    response.insert(QStringLiteral("data"), json.object());
    PostResponseToWebView(response);
}
}

void SendKioskHeartbeat(int status, const std::wstring& timeString)
{
    if (status < 1 || status > 3)
        return;

    bool expected = false;
    if (!g_heartbeatInFlight.compare_exchange_strong(expected, true))
        return;

    try
    {
        const std::wstring elapsed = timeString;
        std::thread([status, elapsed]()
        {
            struct FlightGuard
            {
                ~FlightGuard()
                {
                    g_heartbeatInFlight.store(false);
                }
            } flightGuard;

            const std::wstring configuredIp = ReadServerControlIp();
            if (configuredIp.empty())
                return;

            WSADATA wsaData{};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
                return;

            const std::string ip(configuredIp.begin(), configuredIp.end());
            QJsonObject data;
            data.insert(QStringLiteral("status"), status);
            data.insert(QStringLiteral("time_string"), QString::fromStdWString(elapsed));
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("heartbeat"));
            payload.insert(QStringLiteral("data"), data);
            const std::string body =
                QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString();

            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(kHeartbeatRequestTimeoutMs);
            const HttpResponse response = SendHttpRequest(
                ip, "POST", "/api/v1/health", body, deadline);
            WSACleanup();

            if (!response.received || response.status != 200)
                OutputDebugStringW(L"[HEARTBEAT] No se pudo enviar el estado al servidor central.\n");
        }).detach();
    }
    catch (...)
    {
        g_heartbeatInFlight.store(false);
        OutputDebugStringW(L"[HEARTBEAT] No se pudo crear el worker de heartbeat.\n");
    }
}

void ResizeWebView()
{
    if (g_controller && g_mainHwnd)
    {
        RECT bounds{};
        GetClientRect(g_mainHwnd, &bounds);
        g_controller->put_Bounds(bounds);
    }
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

    const std::wstring serializedJson(message);
    CoTaskMemFree(message);

    const QJsonDocument document = QJsonDocument::fromJson(
        QString::fromStdWString(serializedJson).toUtf8()
    );
    if (!document.isObject())
    {
        OutputDebugStringW(L"[WEBVIEW2] Mensaje rechazado: JSON inválido.\n");
        return;
    }

    const QJsonObject object = document.object();
    const QString action = object.value(QStringLiteral("action")).toString();

    if (action == QStringLiteral("unlock"))
    {
        PostMessageW(g_mainHwnd, WM_KIOSK_UNLOCK, 0, 0);
        return;
    }
    if (action == QStringLiteral("lock"))
    {
        PostMessageW(g_mainHwnd, WM_KIOSK_LOCK, 0, 0);
        return;
    }
    if (action == QStringLiteral("close"))
    {
        PostMessageW(g_mainHwnd, WM_KIOSK_CLOSE, 0, 0);
        return;
    }
    if (action != QStringLiteral("central_api_request"))
    {
        OutputDebugStringW(L"[WEBVIEW2] Mensaje rechazado: acción desconocida.\n");
        return;
    }

    const QString requestId = object.value(QStringLiteral("requestId")).toString();
    const QString method = object.value(QStringLiteral("method")).toString().toUpper();
    const QString path = object.value(QStringLiteral("path")).toString();
    const QJsonValue payloadValue = object.value(QStringLiteral("payload"));

    if (requestId.isEmpty() || requestId.size() > 128
        || (method != QStringLiteral("GET") && method != QStringLiteral("POST"))
        || !IsAllowedApiPath(path.toStdString())
        || (method == QStringLiteral("POST") && !payloadValue.isObject()))
    {
        const QJsonObject response = BuildTransportError(
            requestId,
            QStringLiteral("invalid_request"),
            QStringLiteral("La solicitud al servidor central no es válida.")
        );
        PostResponseToWebView(response);
        return;
    }

    const QJsonObject payload = payloadValue.toObject();
    std::thread([requestId, method, path, payload]()
    {
        ProcessCentralApiRequest(requestId, method, path, payload);
    }).detach();
}

void DispatchCentralApiResponse(std::wstring* responseJson)
{
    if (responseJson == nullptr)
        return;

    if (g_webview)
        g_webview->PostWebMessageAsJson(responseJson->c_str());

    delete responseJson;
}

void DispatchServerIpReceived()
{
    // La recepción puede ocurrir antes de que termine la navegación inicial.
    // Se conserva cada evento para que también genere alerta en ese caso.
    if (!g_webview || !g_pageReady)
    {
        if (g_pendingServerIpReceived < 1000)
            ++g_pendingServerIpReceived;
        return;
    }

    const std::wstring message =
        L"{\"type\":\"server-ip-received\",\"message\":\"La IP del servidor central se recibió y guardó correctamente.\"}";
    g_webview->PostWebMessageAsJson(message.c_str());
}

void DispatchIdleWarning()
{
    if (!g_webview || !g_pageReady)
    {
        g_pendingIdleWarningState = 1;
        return;
    }

    g_pendingIdleWarningState = 0;
    const std::wstring message =
        L"{\"type\":\"idle-warning\",\"message\":\"El kiosco se bloqueará en 2 minutos por inactividad. Mueva el mouse o pulse una tecla para cancelar el bloqueo.\"}";
    g_webview->PostWebMessageAsJson(message.c_str());
}

void DispatchIdleWarningCleared()
{
    if (!g_webview || !g_pageReady)
    {
        g_pendingIdleWarningState = 2;
        return;
    }

    g_pendingIdleWarningState = 0;
    const std::wstring message = L"{\"type\":\"idle-warning-cleared\"}";
    g_webview->PostWebMessageAsJson(message.c_str());
}

namespace
{
void PostStartupFailure(HWND hwnd, const std::wstring& message) noexcept
{
    if (!IsWindow(hwnd))
        return;

    auto* payload = new (std::nothrow) std::wstring(message);
    if (payload == nullptr)
    {
        PostMessageW(hwnd, WM_KIOSK_STARTUP_FAILED, 0, 0);
        return;
    }

    if (!PostMessageW(
            hwnd,
            WM_KIOSK_STARTUP_FAILED,
            0,
            reinterpret_cast<LPARAM>(payload)))
    {
        delete payload;
    }
}

std::wstring FormatWebViewFailure(const wchar_t* stage, HRESULT result)
{
    wchar_t message[384]{};
    swprintf_s(
        message,
        L"%ls. Código HRESULT: 0x%08lX",
        stage,
        static_cast<unsigned long>(result)
    );
    return message;
}
}

void ShutdownWebView2()
{
    g_pageReady = false;
    g_pendingServerIpReceived = 0;
    g_pendingIdleWarningState = 0;

    // Los objetos WebView2 se liberan en el mismo hilo STA en que fueron creados.
    g_webview.Reset();
    g_controller.Reset();
    g_mainHwnd = nullptr;

    if (g_comInitialized)
    {
        g_comInitialized = false;
        CoUninitialize();
    }
}

void InitializeWebView2(HWND hwnd)
{
    g_mainHwnd = hwnd;
    g_pageReady = false;

    if (!IsWindow(hwnd))
    {
        PostStartupFailure(hwnd, L"No se pudo iniciar WebView2 porque la ventana principal no existe.");
        return;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        PostStartupFailure(
            hwnd,
            FormatWebViewFailure(
                L"No se pudo inicializar COM en el apartamento STA requerido por WebView2",
                comResult
            )
        );
        return;
    }
    g_comInitialized = true;

    wchar_t tempPath[MAX_PATH]{};
    const DWORD pathLen = GetTempPathW(MAX_PATH, tempPath);

    std::wstring userDataFolder;
    if (pathLen == 0 || pathLen >= MAX_PATH)
    {
        PostStartupFailure(
            hwnd,
            L"No se pudo determinar una carpeta temporal válida para los datos de WebView2."
        );
        return;
    }

    userDataFolder = std::wstring(tempPath) + L"DesktopKiosk_WebView2";

    const HRESULT environmentResult = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userDataFolder.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
            {
                if (!IsWindow(hwnd))
                    return E_ABORT;

                if (FAILED(result) || environment == nullptr)
                {
                    const HRESULT failure = FAILED(result) ? result : E_POINTER;
                    PostStartupFailure(
                        hwnd,
                        FormatWebViewFailure(
                            L"No se pudo inicializar el entorno de Microsoft Edge WebView2",
                            failure
                        )
                    );
                    return failure;
                }

                const HRESULT controllerCreationResult = environment->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (!IsWindow(hwnd))
                                return E_ABORT;

                            if (FAILED(controllerResult) || controller == nullptr)
                            {
                                const HRESULT failure = FAILED(controllerResult)
                                    ? controllerResult
                                    : E_POINTER;
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo crear el controlador de WebView2",
                                        failure
                                    )
                                );
                                return failure;
                            }

                            g_controller = controller;

                            HRESULT hr = controller->add_AcceleratorKeyPressed(
                                Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                    [](ICoreWebView2Controller*,
                                       ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT
                                    {
                                        if (args == nullptr)
                                            return E_POINTER;

                                        COREWEBVIEW2_KEY_EVENT_KIND eventKind{};
                                        UINT virtualKey = 0;
                                        INT keyEventLParam = 0;

                                        if (SUCCEEDED(args->get_KeyEventKind(&eventKind)) &&
                                            eventKind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                                            SUCCEEDED(args->get_VirtualKey(&virtualKey)) &&
                                            virtualKey == VK_F4 &&
                                            SUCCEEDED(args->get_KeyEventLParam(&keyEventLParam)) &&
                                            (keyEventLParam & (1L << 29)) != 0)
                                        {
                                            return args->put_Handled(TRUE);
                                        }

                                        return S_OK;
                                    })
                                .Get(),
                                nullptr
                            );
                            if (FAILED(hr))
                            {
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo registrar el control de atajos de WebView2",
                                        hr
                                    )
                                );
                                return hr;
                            }

                            hr = controller->get_CoreWebView2(&g_webview);
                            if (FAILED(hr) || !g_webview)
                            {
                                const HRESULT failure = FAILED(hr) ? hr : E_POINTER;
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo obtener la instancia principal de WebView2",
                                        failure
                                    )
                                );
                                return failure;
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            hr = g_webview->get_Settings(&settings);
                            if (FAILED(hr) || !settings)
                            {
                                const HRESULT failure = FAILED(hr) ? hr : E_POINTER;
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo obtener la configuración de WebView2",
                                        failure
                                    )
                                );
                                return failure;
                            }

                            hr = settings->put_AreDevToolsEnabled(FALSE);
                            if (FAILED(hr))
                            {
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudieron deshabilitar las herramientas de desarrollo de WebView2",
                                        hr
                                    )
                                );
                                return hr;
                            }

                            hr = settings->put_AreDefaultContextMenusEnabled(FALSE);
                            if (FAILED(hr))
                            {
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo deshabilitar el menú contextual de WebView2",
                                        hr
                                    )
                                );
                                return hr;
                            }

                            RECT bounds{};
                            if (!GetClientRect(hwnd, &bounds))
                            {
                                const HRESULT failure = HRESULT_FROM_WIN32(GetLastError());
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo obtener el área cliente de la ventana de DesktopKiosk",
                                        failure
                                    )
                                );
                                return failure;
                            }

                            hr = controller->put_Bounds(bounds);
                            if (FAILED(hr))
                            {
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo ajustar WebView2 al tamaño de la ventana",
                                        hr
                                    )
                                );
                                return hr;
                            }

                            hr = g_webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [hwnd](ICoreWebView2*,
                                           ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                                    {
                                        if (!IsWindow(hwnd))
                                            return E_ABORT;

                                        if (args == nullptr)
                                        {
                                            PostStartupFailure(
                                                hwnd,
                                                L"WebView2 notificó una navegación sin información de resultado."
                                            );
                                            return E_POINTER;
                                        }

                                        BOOL navigationSucceeded = FALSE;
                                        HRESULT navigationResult =
                                            args->get_IsSuccess(&navigationSucceeded);
                                        if (FAILED(navigationResult))
                                        {
                                            PostStartupFailure(
                                                hwnd,
                                                FormatWebViewFailure(
                                                    L"No se pudo comprobar el resultado de la navegación inicial de WebView2",
                                                    navigationResult
                                                )
                                            );
                                            return navigationResult;
                                        }

                                        if (!navigationSucceeded)
                                        {
                                            COREWEBVIEW2_WEB_ERROR_STATUS webError{};
                                            const HRESULT statusResult =
                                                args->get_WebErrorStatus(&webError);
                                            wchar_t message[384]{};
                                            if (SUCCEEDED(statusResult))
                                            {
                                                swprintf_s(
                                                    message,
                                                    L"WebView2 no pudo cargar la página inicial. "
                                                    L"Estado de navegación: %u.",
                                                    static_cast<unsigned int>(webError)
                                                );
                                            }
                                            else
                                            {
                                                swprintf_s(
                                                    message,
                                                    L"WebView2 no pudo cargar la página inicial "
                                                    L"(HRESULT 0x%08lX).",
                                                    static_cast<unsigned long>(statusResult)
                                                );
                                            }
                                            PostStartupFailure(hwnd, message);
                                            return E_FAIL;
                                        }

                                        g_pageReady = true;
                                        if (!PostMessageW(hwnd, WM_KIOSK_WEBVIEW_READY, 0, 0))
                                        {
                                            PostStartupFailure(
                                                hwnd,
                                                L"WebView2 cargó la página, pero no se pudo notificar al proceso principal."
                                            );
                                            return HRESULT_FROM_WIN32(GetLastError());
                                        }

                                        return S_OK;
                                    })
                                .Get(),
                                nullptr
                            );
                            if (FAILED(hr))
                            {
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo registrar el evento de navegación de WebView2",
                                        hr
                                    )
                                );
                                return hr;
                            }

                            hr = g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                                    {
                                        if (args == nullptr)
                                            return E_POINTER;
                                        HandleWebMessage(args);
                                        return S_OK;
                                    })
                                .Get(),
                                nullptr
                            );
                            if (FAILED(hr))
                            {
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"No se pudo registrar el canal de mensajes de WebView2",
                                        hr
                                    )
                                );
                                return hr;
                            }

                            const std::wstring url = GetWebPageUrl();
                            if (url.empty())
                            {
                                PostStartupFailure(
                                    hwnd,
                                    L"No se pudo localizar web/index.html. La interfaz del kiosco no puede iniciar."
                                );
                                return E_FAIL;
                            }

                            hr = g_webview->Navigate(url.c_str());
                            if (FAILED(hr))
                            {
                                PostStartupFailure(
                                    hwnd,
                                    FormatWebViewFailure(
                                        L"WebView2 rechazó la navegación a la página inicial",
                                        hr
                                    )
                                );
                                return hr;
                            }

                            return S_OK;
                        })
                    .Get()
                );
                if (FAILED(controllerCreationResult))
                {
                    PostStartupFailure(
                        hwnd,
                        FormatWebViewFailure(
                            L"CreateCoreWebView2Controller no pudo iniciar el controlador de WebView2",
                            controllerCreationResult
                        )
                    );
                }
                return controllerCreationResult;
            })
        .Get()
    );

    if (FAILED(environmentResult))
    {
        PostStartupFailure(
            hwnd,
            FormatWebViewFailure(
                L"CreateCoreWebView2EnvironmentWithOptions no pudo iniciar WebView2",
                environmentResult
            )
        );
    }
}
