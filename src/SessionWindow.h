#pragma once

#include <windows.h>

// Verifica que QApplication ya esté inicializada para crear widgets Qt.
bool RegisterSessionWindowClass(HINSTANCE hInstance);

// Crea el botón circular flotante y el Popup de sesión con Qt 6.
void CreateSessionWindow(HINSTANCE hInstance);

// Destruye la interfaz y su temporizador al bloquear el kiosco.
void DestroySessionWindow();

// Actualiza el contador de sesión (HH:MM:SS).
void UpdateSessionTime();
