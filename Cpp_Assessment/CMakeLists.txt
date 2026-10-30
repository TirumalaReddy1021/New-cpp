cmake_minimum_required(VERSION 3.22)
project(ProjectACEx LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "Project ACEx requires Microsoft Visual C++ (MSVC) compiler.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Project ACEx targets x64 architecture only. 32-bit builds are unsupported.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(/MP /W4 /permissive-)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)


# Main Core Application Target
add_executable(pickerhost
    src/main.cpp
    src/watchdog.cpp
    src/overlay.cpp
    src/engine.cpp
    src/chat_view.cpp
    src/chat_window.cpp
    src/network_client.cpp
    src/privacy_manager.cpp
    src/image_compression.cpp
    src/config_manager.cpp
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_win32.cpp
    external/imgui/backends/imgui_impl_dx11.cpp
    assets/version.rc
)

target_include_directories(pickerhost
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
)

target_link_directories(pickerhost
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(pickerhost
    PRIVATE
        WebView2Loader.lib
        dwmapi.lib
        d3d11.lib
        d3dcompiler.lib
        gdi32.lib
        gdiplus.lib
        winhttp.lib
        crypt32.lib
        user32.lib
        ole32.lib
        shell32.lib
)

add_executable(PEInjectorHarness src/injector.cpp)
add_library(Dll1 SHARED src/Dll1.cpp)