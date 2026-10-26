#pragma once
#include <windows.h>
#include <string>

// Inicializa el motor WebView2 en la ventana indicada
void InitializeWebView2(HWND hwnd);

// Redimensiona la vista de WebView2 al cambiar el tamaño de la ventana
void ResizeWebView();

// Publica en WebView2 la respuesta de una petición del servidor central.
// Se invoca desde el hilo de UI y libera el objeto recibido.
void DispatchCentralApiResponse(std::wstring* responseJson);

// Muestra en el index la confirmación de recepción de la IP del servidor.
void DispatchServerIpReceived();

// Publica en la interfaz el aviso por inactividad y su cancelación.
void DispatchIdleWarning();
void DispatchIdleWarningCleared();
