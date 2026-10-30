# DesktopKiosk

DesktopKiosk es una aplicación de kiosco para Windows diseñada para restringir el uso del equipo a una interfaz segura y controlada. La aplicación muestra un panel web de acceso, gestiona el bloqueo/desbloqueo del sistema, supervisa la inactividad, y se comunica con un servidor central para autenticación, registro y monitoreo.

## Características

- Modo kiosco completo con ventana en pantalla completa
- Bloqueo de accesos no autorizados y atajos del sistema
- Interfaz web integrada con HTML, CSS y JavaScript
- Soporte para login y registro de usuarios
- Comunicación vía REST API en el puerto 7085
- Monitoreo de inactividad con advertencias y bloqueo automático
- Integración con WebView2 para renderizado de la interfaz
- Control remoto de apagado/reinicio del equipo
- Menú administrativo local para gestión del kiosco

## Stack tecnológico

- C++17
- Qt 6
- WebView2
- CMake
- Windows API
- HTML/CSS/JavaScript

## Requisitos

- Windows 10/11
- CMake 3.20 o superior
- Qt 6.11.2 (o compatible)
- Visual Studio 2022
- WebView2 runtime o SDK disponible en el proyecto
- Compilador MSVC

## Estructura del proyecto

```text
DesktopKiosk/
├── CMakeLists.txt
├── docs/
│   └── REST.md
├── extern/
│   └── webview2/
├── src/
│   ├── HttpServer.cpp
│   ├── HttpServer.h
│   ├── KioskMessages.h
│   ├── SessionWindow.cpp
│   ├── SessionWindow.h
│   ├── SystemUtils.cpp
│   ├── SystemUtils.h
│   ├── WebViewManager.cpp
│   ├── WebViewManager.h
│   ├── main.cpp
│   ├── resources.h
│   ├── resources.rc
│   └── app_icon.ico
├── web/
│   ├── app.js
│   ├── index.html
│   ├── style.css
│   └── imgs/
├── .gitignore
└── README.md
```

## Compilación

1. Clona el repositorio:

```bash
git clone https://github.com/mhz698a/DesktopKiosk.git
cd DesktopKiosk
```

2. Crea un directorio de compilación:

```bash
mkdir build
cd build
```

3. Configura el proyecto con CMake:

```bash
cmake .. -G "Visual Studio 17 2022"
```

4. Compila la solución:

```bash
cmake --build . --config Release
```

5. Ejecuta la aplicación compilada.

> Nota: El proyecto está preparado para compilarse en Windows y hace uso de rutas específicas de Qt en CMake. Si tu entorno no coincide con la configuración actual, será necesario ajustar la ruta de `windeployqt` y la instalación de Qt.

## Uso

La aplicación inicializa un kiosco en pantalla completa y muestra una interfaz web localizada en la carpeta `web/`. Desde ahí, el usuario puede:

- iniciar sesión
- registrarse como alumno o externo
- interactuar con el sistema mediante la API central
- usar el menú de administración según las credenciales locales

Durante la ejecución, la app:

- controla el estado del kiosco
- bloquea accesos directos peligrosos
- evita cierres no autorizados
- monitoriza la inactividad
- comunica el estado del equipo a un servidor central

## API REST

El proyecto incluye un servidor HTTP local en el puerto `7085`. La documentación detallada de los endpoints está en:

- `docs/REST.md`

La API se usa para controlar el kiosco, recibir datos del servidor central y gestionar acciones como:

- desbloquear
- bloquear
- cerrar
- registrar IP del servidor central
- reinicio o apagado del sistema

## Seguridad

DesktopKiosk implementa varios mecanismos para reducir la posibilidad de salida del modo kiosco:

- bloqueo de combinaciones como Windows, Alt+Tab, Ctrl+Esc y otras secuencias de acceso
- ventana en modo fullscreen tipo `WS_POPUP`
- manejo de cierre de ventana restringido
- validación de mensajes desde la interfaz web
- control de autenticación por token y validación de peticiones HTTP

## Documentación adicional

Consulta:

- `docs/REST.md` para la referencia de la REST API
- `web/` para la interfaz gráfica
- `src/` para la lógica del clienteWindows

## Estado del proyecto

Este proyecto está enfocado a uso institucional o empresarial en entornos controlados. Está pensado para ejecutarse en estaciones Windows con acceso limitado y supervisión central.

## Licencia

Este repositorio no incluye un archivo de licencia visible en la raíz. Si deseas publicar o distribuir el proyecto públicamente, se recomienda agregar una licencia adecuada antes de compartirlo abiertamente.

## Autor

- mhz698a
