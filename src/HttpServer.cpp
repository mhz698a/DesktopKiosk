#include "resources.h"
#include "HttpServer.h"
#include "SystemUtils.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace
{
constexpr size_t kMaxRequestBytes = 16 * 1024;
constexpr char kDefaultBindAddress[] = "0.0.0.0";
constexpr DWORD kSocketTimeoutMs = 5000;
constexpr size_t kMaxQueuedClients = 32;
constexpr size_t kWorkerCount = 8;

std::string GetEnvironmentValue(const char* name)
{
    char* value = nullptr;
    size_t size = 0;

    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
        return {};

    std::string result(value);
    free(value);
    return result;
}

bool ConstantTimeEquals(const std::string& left, const std::string& right)
{
    if (left.size() != right.size())
        return false;

    unsigned char difference = 0;
    for (size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);

    return difference == 0;
}

std::string BuildResponse(int status, const char* reason, const std::string& body, const char* contentType = "application/json; charset=utf-8")
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
    return response.str();
}

int GetImageResourceId(const std::string& path)
{
    if (path == "/imgs/fondo1.png")
        return IDR_FONDO1_PNG;
    if (path == "/imgs/fondo2.png")
        return IDR_FONDO2_PNG;
    if (path == "/imgs/logo_1.png")
        return IDR_LOGO_1_PNG;
    if (path == "/imgs/logo_2.png")
        return IDR_LOGO_2_PNG;
    if (path == "/imgs/mosaico.png")
        return IDR_MOSAICO_PNG;

    return 0;
}

bool HeaderNameEquals(const std::string& actual, const std::string& expected)
{
    if (actual.size() != expected.size())
        return false;

    return std::equal(actual.begin(), actual.end(), expected.begin(),
        [](unsigned char left, unsigned char right)
        {
            return std::tolower(left) == std::tolower(right);
        });
}

std::string GetHeaderValue(const std::string& request, const std::string& name)
{
    size_t start = 0;

    while (start < request.size())
    {
        const size_t end = request.find("\r\n", start);
        if (end == std::string::npos)
            break;

        const size_t colon = request.find(':', start);
        if (colon != std::string::npos && colon < end)
        {
            const std::string headerName = request.substr(start, colon - start);
            if (HeaderNameEquals(headerName, name))
            {
                size_t valueStart = colon + 1;
                while (valueStart < end && (request[valueStart] == ' ' || request[valueStart] == '\t'))
                    ++valueStart;

                return request.substr(valueStart, end - valueStart);
            }
        }

        start = end + 2;
    }

    return {};
}

std::string GetRequestBody(const std::string& request)
{
    const size_t headerEnd = request.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return {};

    return request.substr(headerEnd + 4);
}

bool IsValidIPv4(const std::string& address)
{
    IN_ADDR parsedAddress{};
    return !address.empty() && InetPtonA(AF_INET, address.c_str(), &parsedAddress) == 1;
}

bool SaveServerControlIp(const std::string& ipAddress)
{
    // APPDATA ya apunta a AppData\Roaming para el usuario actual.
    const DWORD requiredSize = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (requiredSize == 0)
        return false;

    std::wstring appDataPath(requiredSize, L'\0');
    const DWORD copiedSize = GetEnvironmentVariableW(
        L"APPDATA",
        appDataPath.data(),
        requiredSize
    );

    if (copiedSize == 0 || copiedSize >= requiredSize)
        return false;

    appDataPath.resize(copiedSize);

    const std::filesystem::path settingsDirectory =
        std::filesystem::path(appDataPath) / L"DesktopKiosk";

    std::error_code error;
    std::filesystem::create_directories(settingsDirectory, error);
    if (error)
        return false;

    const std::filesystem::path settingsFile = settingsDirectory / L"settings.ini";
    const std::wstring wideIpAddress(ipAddress.begin(), ipAddress.end());

    return WritePrivateProfileStringW(
        L"network",
        L"server_control_ip",
        wideIpAddress.c_str(),
        settingsFile.c_str()
    ) != FALSE;
}


void ConfigureSocketTimeouts(SOCKET client)
{

    setsockopt(
        client,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout),
        sizeof(timeout)
    );

    setsockopt(
        client,
        SOL_SOCKET,
        SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout),
        sizeof(timeout)
    );
}

void SendResponse(SOCKET client, const std::string& response)
{
    size_t sent = 0;

    while (sent < response.size())
    {
        const int result = send(
            client,
            response.data() + sent,
            static_cast<int>(response.size() - sent),
            0
        );

        if (result <= 0)
            break;

        sent += static_cast<size_t>(result);
    }
}

bool ReceiveRequest(SOCKET client, std::string& request)
{
    char buffer[4096];
    request.clear();

    size_t expectedSize = 0;
    bool headersParsed = false;

    while (request.size() < kMaxRequestBytes)
    {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received == SOCKET_ERROR || received == 0)
            return false;

        if (request.size() + static_cast<size_t>(received) > kMaxRequestBytes)
            return false;

        request.append(buffer, static_cast<size_t>(received));

        if (!headersParsed)
        {
            const size_t headerEnd = request.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
                continue;

            // Include the terminating CRLF to parse the final header line.
            const std::string headers = request.substr(0, headerEnd + 2);
            if (!GetHeaderValue(headers, "Transfer-Encoding").empty())
                return false; // Chunked request bodies are not supported.

            size_t contentLength = 0;
            const std::string contentLengthText = GetHeaderValue(headers, "Content-Length");
            if (!contentLengthText.empty())
            {
                const char* first = contentLengthText.data();
                const char* last = first + contentLengthText.size();
                const auto parsed = std::from_chars(first, last, contentLength);

                if (parsed.ec != std::errc{} || parsed.ptr != last)
                    return false;
            }

            const size_t bodyStart = headerEnd + 4;
            if (contentLength > kMaxRequestBytes - bodyStart)
                return false;

            expectedSize = bodyStart + contentLength;
            headersParsed = true;
        }

        if (headersParsed && request.size() >= expectedSize)
            return true;
    }

    return false;
}
}

class HttpServer::Impl
{
public:
    SOCKET listenSocket = INVALID_SOCKET;
    std::thread acceptWorker;
    std::vector<std::thread> workers;
    std::deque<SOCKET> clientQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::atomic<bool> running{false};
    RequestHandler handler;
    std::string apiToken;

    void HandleClient(SOCKET client)
    {
        ConfigureSocketTimeouts(client);

        std::string request;
        if (!ReceiveRequest(client, request))
        {
            SendResponse(client, BuildResponse(408, "Request Timeout", R"({"error":"request_timeout"})"));
            closesocket(client);
            return;
        }

        const size_t requestLineEnd = request.find("\r\n");
        if (requestLineEnd == std::string::npos)
        {
            SendResponse(client, BuildResponse(400, "Bad Request", R"({"error":"invalid_request"})"));
            closesocket(client);
            return;
        }

        std::istringstream requestLine(request.substr(0, requestLineEnd));
        std::string method;
        std::string path;
        std::string version;
        requestLine >> method >> path >> version;

        if ((method != "GET" && method != "POST") || version != "HTTP/1.1")
        {
            SendResponse(client, BuildResponse(405, "Method Not Allowed", R"({"error":"method_not_allowed"})"));
            closesocket(client);
            return;
        }

        if (method == "GET")
        {
            if (path == "/" || path == "/index.html")
            {
                std::string htmlContent = GetEmbeddedResource(IDR_INDEX_HTML);

                if (htmlContent.empty())
                {
                    SendResponse(client, BuildResponse(404, "Not Found", R"({"error":"resource_not_found"})"));
                }
                else
                {
                    SendResponse(client, BuildResponse(200, "OK", htmlContent, "text/html; charset=utf-8"));
                }

                closesocket(client);
                return;
            }
            else if (path == "/style.css")
            {
                std::string cssContent = GetEmbeddedResource(IDR_STYLE_CSS);

                if (cssContent.empty())
                {
                    SendResponse(client, BuildResponse(404, "Not Found", R"({"error":"resource_not_found"})"));
                }
                else
                {
                    SendResponse(client, BuildResponse(200, "OK", cssContent, "text/css; charset=utf-8"));
                }

                closesocket(client);
                return;
            }
            else if (path == "/app.js")
            {
                std::string jsContent = GetEmbeddedResource(IDR_APP_JS);

                if (jsContent.empty())
                {
                    SendResponse(client, BuildResponse(404, "Not Found", R"({"error":"resource_not_found"})"));
                }
                else
                {
                    SendResponse(client, BuildResponse(200, "OK", jsContent, "application/javascript; charset=utf-8"));
                }

                closesocket(client);
                return;
            }
            else
            {
                const int imageResourceId = GetImageResourceId(path);
                if (imageResourceId != 0)
                {
                    std::string imageContent = GetEmbeddedResource(imageResourceId);

                    if (imageContent.empty())
                    {
                        SendResponse(client, BuildResponse(404, "Not Found", R"({"error":"resource_not_found"})"));
                    }
                    else
                    {
                        SendResponse(client, BuildResponse(200, "OK", imageContent, "image/png"));
                    }
                }
                else
                {
                    SendResponse(client, BuildResponse(404, "Not Found", R"({"error":"not_found"})"));
                }

                closesocket(client);
                return;
            }
        }

        // All POST requests require the configured API token.
        const size_t headerEnd = request.find("\r\n\r\n");
        const std::string headers = headerEnd == std::string::npos
            ? request
            : request.substr(0, headerEnd + 2);
        const std::string authorization = GetHeaderValue(headers, "Authorization");
        const std::string expectedAuthorization = "Bearer " + apiToken;

        if (apiToken.empty() || !ConstantTimeEquals(authorization, expectedAuthorization))
        {
            SendResponse(client, BuildResponse(401, "Unauthorized", R"({"error":"unauthorized"})"));
            closesocket(client);
            return;
        }


        if (path == "/api/kiosk/receive")
        {
            const std::string body = GetRequestBody(request);
            QJsonParseError parseError{};
            const QJsonDocument json = QJsonDocument::fromJson(
                QByteArray::fromStdString(body),
                &parseError
            );

            if (parseError.error != QJsonParseError::NoError || !json.isObject())
            {
                SendResponse(client, BuildResponse(400, "Bad Request", R"({"error":"invalid_json"})"));
                closesocket(client);
                return;
            }

            const QJsonObject message = json.object();
            if (message.value(QStringLiteral("action")).toString() != QStringLiteral("receive")
                || !message.value(QStringLiteral("data")).isObject())
            {
                SendResponse(client, BuildResponse(400, "Bad Request", R"({"error":"invalid_payload"})"));
                closesocket(client);
                return;
            }

            const QJsonObject data = message.value(QStringLiteral("data")).toObject();
            const QJsonValue ipValue = data.value(QStringLiteral("server_control_ip"));
            const std::string serverControlIp = ipValue.isString()
                ? ipValue.toString().trimmed().toStdString()
                : std::string{};

            if (!IsValidIPv4(serverControlIp))
            {
                SendResponse(client, BuildResponse(400, "Bad Request", R"({"error":"invalid_server_control_ip"})"));
                closesocket(client);
                return;
            }

            if (!SaveServerControlIp(serverControlIp))
            {
                SendResponse(client, BuildResponse(500, "Internal Server Error", R"({"error":"settings_write_failed"})"));
                closesocket(client);
                return;
            }

            const std::string responseBody =
                "{\"status\":\"accepted\",\"action\":\"receive\",\"server_control_ip\":\""
                + serverControlIp + "\"}";
            SendResponse(client, BuildResponse(202, "Accepted", responseBody));
            closesocket(client);
            return;
        }

        std::string action;
        if (path == "/api/kiosk/unlock")
            action = "unlock";
        else if (path == "/api/kiosk/lock")
            action = "lock";
        else if (path == "/api/kiosk/close")
            action = "close";
        else
        {
            SendResponse(client, BuildResponse(404, "Not Found", R"({"error":"not_found"})"));
            closesocket(client);
            return;
        }

        if (handler)
            handler(action);

        SendResponse(
            client,
            BuildResponse(
                202,
                "Accepted",
                "{\"status\":\"accepted\",\"action\":\"" + action + "\"}"
            )
        );

        closesocket(client);
    }

    void ClientWorker()
    {
        while (true)
        {
            SOCKET client = INVALID_SOCKET;

            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCondition.wait(lock, [this]
                {
                    return !running || !clientQueue.empty();
                });

                if (!running)
                    return;

                client = clientQueue.front();
                clientQueue.pop_front();
            }

            HandleClient(client);
        }
    }

    void Run()
    {
        while (running)
        {
            sockaddr_in clientAddress{};
            int clientAddressLength = sizeof(clientAddress);

            SOCKET client = accept(
                listenSocket,
                reinterpret_cast<sockaddr*>(&clientAddress),
                &clientAddressLength
            );

            if (client == INVALID_SOCKET)
            {
                if (running)
                    OutputDebugStringA("[HTTP] accept() fallo.\n");
                continue;
            }

            if (!running)
            {
                closesocket(client);
                break;
            }

            bool queued = false;
            {
                std::lock_guard<std::mutex> lock(queueMutex);

                if (clientQueue.size() < kMaxQueuedClients)
                {
                    clientQueue.push_back(client);
                    queued = true;
                }
            }

            if (queued)
                queueCondition.notify_one();
            else
            {
                ConfigureSocketTimeouts(client);
                SendResponse(client, BuildResponse(503, "Service Unavailable", R"({"error":"server_busy"})"));
                closesocket(client);
            }
        }
    }
};

HttpServer::~HttpServer()
{
    Stop();
}

bool HttpServer::Start(unsigned short port, RequestHandler handler)
{
    if (m_impl != nullptr)
        return false;

    const std::string token = EnsureClientApiToken();

    const std::string bindAddress = [&]()
    {
        const std::string configured = GetEnvironmentValue("DESKTOPKIOSK_HTTP_BIND");
        return configured.empty() ? std::string(kDefaultBindAddress) : configured;
    }();

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    auto* impl = new Impl();
    impl->handler = std::move(handler);
    impl->apiToken = token;

    impl->listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl->listenSocket == INVALID_SOCKET)
    {
        delete impl;
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1)
    {
        OutputDebugStringA("[HTTP] DESKTOPKIOSK_HTTP_BIND no es una direccion IPv4 valida.\n");
        closesocket(impl->listenSocket);
        delete impl;
        WSACleanup();
        return false;
    }

    int reuse = 1;
    setsockopt(
        impl->listenSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse)
    );

    if (bind(
        impl->listenSocket,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    ) == SOCKET_ERROR)
    {
        closesocket(impl->listenSocket);
        delete impl;
        WSACleanup();
        return false;
    }

    if (listen(impl->listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        closesocket(impl->listenSocket);
        delete impl;
        WSACleanup();
        return false;
    }

    impl->running = true;

    for (size_t i = 0; i < kWorkerCount; ++i)
        impl->workers.emplace_back([impl]() { impl->ClientWorker(); });

    impl->acceptWorker = std::thread([impl]()
    {
        impl->Run();
    });

    m_impl = impl;

    OutputDebugStringA("[HTTP] REST escuchando en puerto 7085.\n");
    return true;
}

void HttpServer::Stop()
{
    if (m_impl == nullptr)
        return;

    m_impl->running = false;

    if (m_impl->listenSocket != INVALID_SOCKET)
    {
        shutdown(m_impl->listenSocket, SD_BOTH);
        closesocket(m_impl->listenSocket);
    }

    if (m_impl->acceptWorker.joinable())
        m_impl->acceptWorker.join();

    m_impl->listenSocket = INVALID_SOCKET;

    m_impl->queueCondition.notify_all();

    for (auto& worker : m_impl->workers)
    {
        if (worker.joinable())
            worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->queueMutex);
        while (!m_impl->clientQueue.empty())
        {
            closesocket(m_impl->clientQueue.front());
            m_impl->clientQueue.pop_front();
        }
    }

    delete m_impl;
    m_impl = nullptr;

    WSACleanup();
}
