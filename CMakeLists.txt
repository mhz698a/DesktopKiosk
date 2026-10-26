cmake_minimum_required(VERSION 3.20)

project(DesktopKiosk LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(DesktopKiosk WIN32
    src/main.cpp
    src/SystemUtils.h
    src/SystemUtils.cpp
    src/SessionWindow.h
    src/SessionWindow.cpp
    src/WebViewManager.h
    src/WebViewManager.cpp
    src/KioskMessages.h
    src/HttpServer.h
    src/HttpServer.cpp
    src/resources.h
    src/resources.rc
)

target_compile_definitions(DesktopKiosk PRIVATE
    UNICODE
    _UNICODE
)

set(WEBVIEW2_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/webview2/build/native"
)

set(WEBVIEW2_INCLUDE_DIR
    "${WEBVIEW2_ROOT}/include"
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WEBVIEW2_ARCH_DIR
        "${WEBVIEW2_ROOT}/x64"
    )
else()
    set(WEBVIEW2_ARCH_DIR
        "${WEBVIEW2_ROOT}/x86"
    )
endif()

target_include_directories(DesktopKiosk PRIVATE
    "${WEBVIEW2_INCLUDE_DIR}"
)

target_link_directories(DesktopKiosk PRIVATE
    "${WEBVIEW2_ARCH_DIR}"
)

target_link_libraries(DesktopKiosk PRIVATE
    user32
    ole32
    oleaut32
    shlwapi
    iphlpapi
    advapi32
    ws2_32
    version
    runtimeobject
    "${WEBVIEW2_ARCH_DIR}/WebView2LoaderStatic.lib"
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# --- MÉTODO DE DESPLIEGUE DIRECTO CON WINDEPLOYQT Y ENIGMA ---
if(WIN32)
    # 1. Definimos la ruta directa a tu ejecutable de windeployqt
    set(_qt_bin_dir "C:/Qt/6.11.2/msvc2022_64/bin")
    
    # 2. Definimos las rutas para Enigma Virtual Box
    set(_enigma_console "C:/Program Files (x86)/Enigma Virtual Box/enigmavbconsole.exe")
    set(_evb_project "${CMAKE_CURRENT_SOURCE_DIR}/proyect_box.evb")

    # Comando post-construcción que ejecuta ambos pasos en orden continuo
    add_custom_command(TARGET DesktopKiosk POST_BUILD 
        COMMAND "${_qt_bin_dir}/windeployqt.exe" --debug "$<TARGET_FILE:DesktopKiosk>"
        COMMAND "${_enigma_console}" "${_evb_project}"
        COMMENT "Paso 1: Copiando DLLs de Qt... | Paso 2: Empaquetando ejecutable único con Enigma..."
    )
endif()
