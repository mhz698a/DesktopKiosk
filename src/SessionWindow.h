#pragma once

#include <windows.h>

// Registra las clases Win32 del botón circular y del panel emergente de sesión.
bool RegisterSessionWindowClass(HINSTANCE hInstance);

// Crea el botón flotante persistente y el panel de sesión, inicialmente oculto.
void CreateSessionWindow(HINSTANCE hInstance);

// Destruye el botón, el panel y el temporizador de la sesión actual.
void DestroySessionWindow();

// Actualiza el contador de tiempo transcurrido (HH:MM:SS).
void UpdateSessionTime();

// Manejador de eventos del panel emergente de sesión.
LRESULT CALLBACK SessionWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
