#include "SystemUtils.h"
#include "resources.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <filesystem>
#include <system_error>
#include <vector>
#include <string>

#pragma comment(lib, "shlwapi.lib")

std::string EnsureClientApiToken()
{
    // Token integrado al ejecutable y sincronizado con la configuración local.
    constexpr wchar_t kDefaultApiToken[] = L"UAQinformatizacion7085";
    constexpr char kDefaultApiTokenAscii[] = "UAQinformatizacion7085";

    const DWORD requiredSize = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (requiredSize == 0)
    {
        OutputDebugStringW(L"[SETTINGS] No se pudo resolver APPDATA; se usara el token integrado.\n");
        return kDefaultApiTokenAscii;
    }

    std::wstring appDataPath(requiredSize, L'\0');
    const DWORD copiedSize = GetEnvironmentVariableW(L"APPDATA", appDataPath.data(), requiredSize);
    if (copiedSize == 0 || copiedSize >= requiredSize)
    {
        OutputDebugStringW(L"[SETTINGS] No se pudo leer APPDATA; se usara el token integrado.\n");
        return kDefaultApiTokenAscii;
    }
    appDataPath.resize(copiedSize);

    const std::filesystem::path settingsDirectory = std::filesystem::path(appDataPath) / L"DesktopKiosk";
    const std::filesystem::path settingsFile = settingsDirectory / L"settings.ini";
    std::error_code error;
    std::filesystem::create_directories(settingsDirectory, error);
    if (error)
    {
        OutputDebugStringW(L"[SETTINGS] No se pudo crear el directorio; se usara el token integrado.\n");
        return kDefaultApiTokenAscii;
    }

    wchar_t installedToken[256] = {};
    GetPrivateProfileStringW(L"security", L"api_token", L"", installedToken,
        static_cast<DWORD>(sizeof(installedToken) / sizeof(installedToken[0])), settingsFile.c_str());

    if (std::wstring(installedToken) != kDefaultApiToken)
    {
        if (!WritePrivateProfileStringW(L"security", L"api_token", kDefaultApiToken, settingsFile.c_str()))
        {
            OutputDebugStringW(L"[SETTINGS] No se pudo actualizar api_token; se usara el token integrado.\n");
        }
        else
        {
            OutputDebugStringW(L"[SETTINGS] Token sincronizado con el ejecutable.\n");
        }
    }

    // El valor esperado para autenticar siempre es el que trae el ejecutable.
    return kDefaultApiTokenAscii;
}

std::wstring GetComputerNameString()
{
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;

    if (GetComputerNameW(computerName, &size))
    {
        return std::wstring(computerName);
    }
    return L"Desconocido";
}

std::wstring GetIpAddress()
{
    ULONG bufferSize = 0;

    DWORD result = GetAdaptersAddresses(
        AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr,
        nullptr,
        &bufferSize
    );

    if (result != ERROR_BUFFER_OVERFLOW)
    {
        return L"No disponible";
    }

    std::vector<BYTE> buffer(bufferSize);
    
    IP_ADAPTER_ADDRESSES* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    result = GetAdaptersAddresses(
        AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr,
        adapters,
        &bufferSize
    );

    if (result != NO_ERROR)
    {
        return L"No disponible";
    }

    for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != nullptr; adapter = adapter->Next)
    {
        if (adapter->OperStatus != IfOperStatusUp)
        {
            continue;
        }

        for (IP_ADAPTER_UNICAST_ADDRESS* address = adapter->FirstUnicastAddress; address != nullptr; address = address->Next)
        {
            if (address->Address.lpSockaddr != nullptr && address->Address.lpSockaddr->sa_family == AF_INET)
            {
                sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);
                wchar_t ipBuffer[INET_ADDRSTRLEN] = {};

                if (InetNtopW(AF_INET, &ipv4->sin_addr, ipBuffer, INET_ADDRSTRLEN) != nullptr)
                {
                    return std::wstring(ipBuffer);
                }
            }
        }
    }

    return L"No disponible";
}

std::wstring GetUserNameString()
{
    wchar_t username[256];
    DWORD size = 256;

    if (GetUserNameW(username, &size))
    {
        return std::wstring(username);
    }

    return L"Desconocido";
}

std::wstring GetWebPageUrl()
{
    return L"http://127.0.0.1:7085/index.html";
}

std::string GetEmbeddedResource(int resourceId) {
    // Buscar primero recursos HTML y luego recursos binarios (RCDATA).
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_HTML);
    if (!hRes)
        hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes)
        return "";

    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData)
        return "";

    const DWORD dataSize = SizeofResource(nullptr, hRes);
    const char* pData = reinterpret_cast<const char*>(LockResource(hData));

    if (!pData)
        return "";

    // std::string conserva los bytes nulos, por lo que también sirve para PNG.
    return std::string(pData, dataSize);
}

