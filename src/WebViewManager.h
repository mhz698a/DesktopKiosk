#pragma once
#include <windows.h>

// Inicializa el motor WebView2 en la ventana indicada
void InitializeWebView2(HWND hwnd);

// Redimensiona la vista de WebView2 al cambiar el tamaño de la ventana
void ResizeWebView();