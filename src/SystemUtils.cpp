#include "SystemUtils.h"
#include "resources.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <netfw.h>
#include <shellapi.h>
#include <oleauto.h>
#include <wrl/client.h>

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


namespace
{
constexpr wchar_t kKioskFirewallRuleName[] = L"DesktopKiosk API 7085";
constexpr wchar_t kKioskFirewallDescription[] = L"Permite el control remoto de DesktopKiosk por TCP 7085 en redes privadas y públicas.";

std::wstring GetExecutablePath()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return std::wstring(buffer.data(), length);
}

bool RunElevatedFirewallCommand(const std::wstring& executablePath)
{
    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT systemDirectoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (systemDirectoryLength == 0 || systemDirectoryLength >= MAX_PATH)
        return false;

    const std::wstring netshPath = std::wstring(systemDirectory, systemDirectoryLength) + L"\\netsh.exe";
    const std::wstring parameters =
        L"advfirewall firewall add rule name=\"" + std::wstring(kKioskFirewallRuleName)
        + L"\" dir=in action=allow program=\"" + executablePath
        + L"\" enable=yes profile=private,public protocol=TCP localport=7085";

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = netshPath.c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;

    if (!ShellExecuteExW(&executeInfo))
    {
        OutputDebugStringW(L"[FIREWALL] No se pudo ejecutar netsh con elevación.\\n");
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    const bool succeeded =
        waitResult == WAIT_OBJECT_0
        && GetExitCodeProcess(executeInfo.hProcess, &exitCode)
        && exitCode == 0;

    CloseHandle(executeInfo.hProcess);
    if (!succeeded)
        OutputDebugStringW(L"[FIREWALL] netsh no pudo crear la regla de entrada.\\n");
    return succeeded;
}

bool FirewallRuleMatches(INetFwRule* rule, const std::wstring& executablePath)
{
    if (rule == nullptr)
        return false;

    BSTR applicationName = nullptr;
    BSTR localPorts = nullptr;
    long protocol = 0;
    long direction = 0;
    long action = 0;
    long profiles = 0;
    VARIANT_BOOL enabled = VARIANT_FALSE;

    const bool succeeded =
        SUCCEEDED(rule->get_ApplicationName(&applicationName))
        && SUCCEEDED(rule->get_LocalPorts(&localPorts))
        && SUCCEEDED(rule->get_Protocol(&protocol))
        && SUCCEEDED(rule->get_Direction(&direction))
        && SUCCEEDED(rule->get_Action(&action))
        && SUCCEEDED(rule->get_Profiles(&profiles))
        && SUCCEEDED(rule->get_Enabled(&enabled));

    const bool matches = succeeded
        && applicationName != nullptr
        && localPorts != nullptr
        && std::wstring(applicationName) == executablePath
        && std::wstring(localPorts) == L"7085"
        && protocol == NET_FW_IP_PROTOCOL_TCP
        && direction == NET_FW_RULE_DIR_IN
        && action == NET_FW_ACTION_ALLOW
        && enabled == VARIANT_TRUE
        && profiles == (NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC);

    SysFreeString(applicationName);
    SysFreeString(localPorts);
    return matches;
}

bool ConfigureFirewallRuleThroughCom(const std::wstring& executablePath)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<INetFwPolicy2> policy;
    HRESULT hr = CoCreateInstance(
        __uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2), reinterpret_cast<void**>(policy.GetAddressOf()));
    if (FAILED(hr) || !policy)
        return false;

    ComPtr<INetFwRules> rules;
    hr = policy->get_Rules(rules.GetAddressOf());
    if (FAILED(hr) || !rules)
        return false;

    BSTR ruleName = SysAllocString(kKioskFirewallRuleName);
    BSTR applicationName = SysAllocString(executablePath.c_str());
    BSTR localPorts = SysAllocString(L"7085");
    BSTR description = SysAllocString(kKioskFirewallDescription);
    if (!ruleName || !applicationName || !localPorts || !description)
    {
        SysFreeString(ruleName);
        SysFreeString(applicationName);
        SysFreeString(localPorts);
        SysFreeString(description);
        return false;
    }

    ComPtr<INetFwRule> rule;
    hr = rules->Item(ruleName, rule.GetAddressOf());
    const bool alreadyExists = SUCCEEDED(hr) && rule.Get() != nullptr;

    if (!alreadyExists)
    {
        rule.Reset();
        hr = CoCreateInstance(
            __uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(INetFwRule), reinterpret_cast<void**>(rule.GetAddressOf()));
    }

    bool configured = false;
    if (SUCCEEDED(hr) && rule)
    {
        if (alreadyExists && FirewallRuleMatches(rule.Get(), executablePath))
        {
            configured = true;
        }
        else
        {
            HRESULT propertyResult = rule->put_Name(ruleName);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_Description(description);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_ApplicationName(applicationName);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_LocalPorts(localPorts);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_Direction(NET_FW_RULE_DIR_IN);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_Action(NET_FW_ACTION_ALLOW);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_Enabled(VARIANT_TRUE);
            if (SUCCEEDED(propertyResult)) propertyResult = rule->put_Profiles(
                NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC);

            if (SUCCEEDED(propertyResult))
            {
                if (alreadyExists)
                    configured = true;
                else
                    configured = SUCCEEDED(rules->Add(rule.Get()));
            }
        }
    }

    SysFreeString(ruleName);
    SysFreeString(applicationName);
    SysFreeString(localPorts);
    SysFreeString(description);
    return configured;
}
}

bool EnsureKioskFirewallRule()
{
    const std::wstring executablePath = GetExecutablePath();
    if (executablePath.empty())
    {
        OutputDebugStringW(L"[FIREWALL] No se pudo determinar la ruta del ejecutable.\\n");
        return false;
    }

    const HRESULT comInitialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = comInitialization == S_OK || comInitialization == S_FALSE;
    bool configured = false;

    if (SUCCEEDED(comInitialization) || comInitialization == RPC_E_CHANGED_MODE)
        configured = ConfigureFirewallRuleThroughCom(executablePath);

    if (shouldUninitialize)
        CoUninitialize();

    // La escritura persistente de reglas puede requerir elevación. Se solicita
    // con UAC únicamente cuando la regla correcta no existe o no se pudo crear.
    if (!configured)
        configured = RunElevatedFirewallCommand(executablePath);

    return configured;
}
