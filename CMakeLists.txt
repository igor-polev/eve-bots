cmake_minimum_required(VERSION 3.12)
project(eve_bot)

set(CMAKE_CXX_STANDARD          14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
set(CUDAToolkit_ROOT /opt/cuda)
find_package(OpenCV        REQUIRED)
find_package(nlohmann_json REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/libv4l2cpp/inc
    ${OpenCV_INCLUDE_DIRS}
)

# libv4l2cpp V4L2 wrapper
aux_source_directory(
    ${PROJECT_SOURCE_DIR}/libv4l2cpp/src
    V4L2CPP_FILES
)
add_library(v4l2cpp STATIC
    ${V4L2CPP_FILES}
)

# project itself
aux_source_directory(
    ${PROJECT_SOURCE_DIR}/source
    ABOT_FILES
)
add_library(abots STATIC
    ${ABOT_FILES}
)

# the program
add_executable(${PROJECT_NAME}
    main.cpp
)
target_link_libraries(${PROJECT_NAME}
    abots
    v4l2cpp
    ${nlohmann_json_LIBS}
    ${OpenCV_LIBS}
)