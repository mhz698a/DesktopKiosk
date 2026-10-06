#pragma once
#include <windows.h>

// Registra la clase de ventana de sesión una sola vez durante la inicialización.
bool RegisterSessionWindowClass(HINSTANCE hInstance);

// Crea e inicializa la ventana flotante de sesión activa.
void CreateSessionWindow(HINSTANCE hInstance);

// Destruye la ventana flotante cuando se vuelve a bloquear el kiosco.
void DestroySessionWindow();

// Actualiza el contador de tiempo transcurrido (HH:MM:SS).
void UpdateSessionTime();

// Manejador de eventos (Window Procedure) de la ventana flotante.
LRESULT CALLBACK SessionWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
