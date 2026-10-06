cmake_minimum_required(VERSION 3.20)

project(DesktopKiosk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(DesktopKiosk WIN32
    src/main.cpp
    src/SystemUtils.h
    src/SystemUtils.cpp
    src/SessionWindow.h
    src/SessionWindow.cpp
    src/WebViewManager.h
    src/WebViewManager.cpp
)

target_compile_definitions(DesktopKiosk PRIVATE
    UNICODE
    _UNICODE
)

# ============================================================
# WebView2 SDK
# ============================================================

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
    WebView2Loader.dll.lib
)

# ============================================================
# Copiar WebView2Loader.dll al directorio del ejecutable
# ============================================================

add_custom_command(TARGET DesktopKiosk POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_if_different

    "${WEBVIEW2_ARCH_DIR}/WebView2Loader.dll"

    "$<TARGET_FILE_DIR:DesktopKiosk>/WebView2Loader.dll"
)

add_custom_command(TARGET DesktopKiosk POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_directory

    "${CMAKE_CURRENT_SOURCE_DIR}/web"

    "$<TARGET_FILE_DIR:DesktopKiosk>/web"
)