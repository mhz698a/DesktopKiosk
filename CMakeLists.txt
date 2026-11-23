cmake_minimum_required(VERSION 3.20)

project(DesktopKiosk LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

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

if(WIN32)
    # Resolver windeployqt desde la misma instalación de Qt que encontró CMake.
    get_filename_component(_qt_package_dir "${Qt6_DIR}" ABSOLUTE)
    get_filename_component(_qt_prefix_dir "${_qt_package_dir}/../../.." ABSOLUTE)
    set(_qt_bin_dir "${_qt_prefix_dir}/bin")
    set(_windeployqt "${_qt_bin_dir}/windeployqt.exe")

    if(NOT EXISTS "${_windeployqt}")
        message(FATAL_ERROR
            "No se encontró windeployqt.exe en ${_qt_bin_dir}. "
            "Comprueba CMAKE_PREFIX_PATH y la instalación de Qt."
        )
    endif()

    add_custom_command(TARGET DesktopKiosk POST_BUILD
        COMMAND "${_windeployqt}" $<$<CONFIG:Debug>:--debug> "$<TARGET_FILE:DesktopKiosk>"
        COMMENT "Desplegando dependencias de Qt con windeployqt..."
    )
endif()