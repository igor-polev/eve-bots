cmake_minimum_required(VERSION 3.21)
project(eve_bots LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/cli.cpp
    src/paths.cpp
    src/settings.cpp
    src/window_finder.cpp
    src/screen_capture.cpp
    src/png_writer.cpp
    src/image_library.cpp
    src/image_detector.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE include)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# WindowsApp - C++/WinRT (Windows Graphics Capture)
# d3d11, dxgi - capture surfaces;  windowscodecs - PNG;  dwmapi - cloak check
target_link_libraries(${PROJECT_NAME} PRIVATE
    nlohmann_json::nlohmann_json
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    WindowsApp
    d3d11
    dxgi
    windowscodecs
    dwmapi
)
