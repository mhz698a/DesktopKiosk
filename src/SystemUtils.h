#pragma once
#include <string>

// Declaraciones de funciones de consulta al sistema y red
std::wstring GetComputerNameString();
std::wstring GetIpAddress();
std::wstring GetUserNameString();
std::wstring GetWebPageUrl();
std::string GetEmbeddedResource(int resourceId);
// Asegura y devuelve el token integrado en el ejecutable.
std::string EnsureClientApiToken();

// Instala o valida la regla entrante del puerto 7085 en perfiles privados y públicos.
bool EnsureKioskFirewallRule();
