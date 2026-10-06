#pragma once
#include <functional>
#include <string>

class HttpServer
{
public:
    using RequestHandler = std::function<void(const std::string& action)>;

    HttpServer() = default;
    ~HttpServer();

    bool Start(unsigned short port, RequestHandler handler);
    void Stop();

private:
    class Impl;
    Impl* m_impl = nullptr;
};
