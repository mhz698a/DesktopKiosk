#include "SystemUtils.h"
#include "resources.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <filesystem>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

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
    // 1. Buscar el recurso dentro del propio módulo EXE
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(resourceId), RT_HTML);
    if (!hRes) return "";

    // 2. Cargar el recurso en la memoria global
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return "";

    // 3. Bloquearlo para obtener el puntero directo a los bytes
    DWORD dataSize = SizeofResource(NULL, hRes);
    const char* pData = reinterpret_cast<const char*>(LockResource(hData));

    if (!pData) return "";

    // 4. Retornar los datos como un string de C++
    return std::string(pData, dataSize);
}