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
#include <chrono>
#include <cctype>
#include <filesystem>
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
constexpr size_t kMaxCentralResponseBytes = 64 * 1024;

HWND g_mainHwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller = nullptr;
ComPtr<ICoreWebView2> g_webview = nullptr;

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

    if (path != QStringLiteral("/api/v1/health"))
    {
        const HttpResponse health = SendHttpRequest(
            ip, "GET", "/api/v1/health", {},
            deadline
        );

        if (!health.received || health.status != 200)
        {
            WSACleanup();
            PostResponseToWebView(BuildTransportError(
                requestId,
                QStringLiteral("central_server_unavailable"),
                QStringLiteral("No se encontro el servidor central, contacte con el administrador de este equipo")
            ));
            return;
        }
    }

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
    if (!g_webview)
        return;

    const std::wstring message =
        L"{\"type\":\"server-ip-received\",\"message\":\"La IP del servidor central se recibió y guardó correctamente.\"}";
    g_webview->PostWebMessageAsJson(message.c_str());
}

void InitializeWebView2(HWND hwnd)
{
    g_mainHwnd = hwnd;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    wchar_t tempPath[MAX_PATH];
    DWORD pathLen = GetTempPathW(MAX_PATH, tempPath);

    std::wstring userDataFolder;
    if (pathLen > 0 && pathLen < MAX_PATH)
    {
        userDataFolder = std::wstring(tempPath) + L"DesktopKiosk_WebView2";
    }

    HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userDataFolder.empty() ? nullptr : userDataFolder.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
            {
                if (FAILED(result))
                {
                    wchar_t errMsg[256];
                    swprintf_s(errMsg, L"No se pudo inicializar WebView2. Codigo: 0x%08X", result);
                    MessageBoxW(hwnd, errMsg, L"DesktopKiosk", MB_ICONERROR);
                    return result;
                }

                return environment->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (FAILED(result))
                            {
                                wchar_t errMsg[256];
                                swprintf_s(errMsg, L"No se pudo crear el controlador de WebView2. Codigo: 0x%08X", result);
                                MessageBoxW(hwnd, errMsg, L"DesktopKiosk", MB_ICONERROR);
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
        wchar_t errMsg[256];
        swprintf_s(errMsg, L"CreateCoreWebView2EnvironmentWithOptions fallo. Codigo: 0x%08X", result);
        MessageBoxW(hwnd, errMsg, L"DesktopKiosk", MB_ICONERROR);
    }
}
