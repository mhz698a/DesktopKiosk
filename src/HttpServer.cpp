#include "HttpServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace
{
constexpr size_t kMaxRequestBytes = 16 * 1024;
constexpr char kDefaultBindAddress[] = "127.0.0.1";

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

std::string BuildResponse(int status, const char* reason, const std::string& body)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
    return response.str();
}

std::string GetHeaderValue(const std::string& request, const std::string& name)
{
    const std::string prefix = name + ":";
    size_t start = 0;

    while (start < request.size())
    {
        const size_t end = request.find("\r\n", start);
        if (end == std::string::npos)
            break;

        if (request.compare(start, prefix.size(), prefix) == 0)
        {
            size_t valueStart = start + prefix.size();
            while (valueStart < end && (request[valueStart] == ' ' || request[valueStart] == '\t'))
                ++valueStart;

            return request.substr(valueStart, end - valueStart);
        }

        start = end + 2;
    }

    return {};
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

    while (request.size() < kMaxRequestBytes)
    {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return false;

        request.append(buffer, received);

        if (request.find("\r\n\r\n") != std::string::npos)
            return true;
    }

    return false;
}
}

class HttpServer::Impl
{
public:
    SOCKET listenSocket = INVALID_SOCKET;
    std::thread worker;
    std::atomic<bool> running{false};
    RequestHandler handler;
    std::string apiToken;

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

            std::string request;
            if (!ReceiveRequest(client, request))
            {
                SendResponse(client, BuildResponse(413, "Payload Too Large", R"({"error":"request_too_large"})"));
                closesocket(client);
                continue;
            }

            const size_t requestLineEnd = request.find("\r\n");
            if (requestLineEnd == std::string::npos)
            {
                SendResponse(client, BuildResponse(400, "Bad Request", R"({"error":"invalid_request"})"));
                closesocket(client);
                continue;
            }

            std::istringstream requestLine(request.substr(0, requestLineEnd));
            std::string method;
            std::string path;
            std::string version;
            requestLine >> method >> path >> version;

            if (method != "POST" || version != "HTTP/1.1")
            {
                SendResponse(client, BuildResponse(405, "Method Not Allowed", R"({"error":"method_not_allowed"})"));
                closesocket(client);
                continue;
            }

            const std::string authorization = GetHeaderValue(request, "Authorization");
            const std::string expected = "Bearer " + apiToken;

            if (!ConstantTimeEquals(authorization, expected))
            {
                SendResponse(client, BuildResponse(401, "Unauthorized", R"({"error":"unauthorized"})"));
                closesocket(client);
                continue;
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
                continue;
            }

            if (handler)
                handler(action);

            SendResponse(client, BuildResponse(202, "Accepted", std::string(R"({"status":"accepted","action":")") + action + ""}"));
            closesocket(client);
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

    const std::string token = GetEnvironmentValue("DESKTOPKIOSK_API_TOKEN");
    if (token.empty())
    {
        OutputDebugStringA("[HTTP] DESKTOPKIOSK_API_TOKEN no esta configurado; REST deshabilitado.\n");
        return false;
    }

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
    impl->worker = std::thread([impl]()
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
        m_impl->listenSocket = INVALID_SOCKET;
    }

    if (m_impl->worker.joinable())
        m_impl->worker.join();

    delete m_impl;
    m_impl = nullptr;

    WSACleanup();
}
