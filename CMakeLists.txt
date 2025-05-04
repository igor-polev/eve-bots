cmake_minimum_required(VERSION 3.10)
project(eve_bot)

set(CMAKE_CXX_STANDARD          14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(OpenCV        REQUIRED)
find_package(nlohmann_json REQUIRED)

# libv4l2cpp V4L2 wrapper
include_directories (libv4l2cpp/inc)
aux_source_directory(libv4l2cpp/src LIBV4L2CPP_FILES)
add_library         (libv4l2cpp STATIC ${LIBV4L2CPP_FILES})

# project itself
include_directories (${PROJECT_SOURCE_DIR}/include)
aux_source_directory(${PROJECT_SOURCE_DIR}/source PROJECTLIB_FILES)
add_library         (projectlib STATIC ${PROJECTLIB_FILES})

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME}
    projectlib
    libv4l2cpp
    ${OpenCV_LIBS}
    ${nlohmann_json_LIBS}
)