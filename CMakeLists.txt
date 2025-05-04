cmake_minimum_required(VERSION 3.10)
project(eve_bot)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libv4l2cpp V4L2 wrapper
include_directories (libv4l2cpp/inc)
aux_source_directory(libv4l2cpp/src LIBV4L2CPP_FILES)
add_library         (libv4l2cpp STATIC ${LIBV4L2CPP_FILES})

# OpenCV image recognition library
find_package(OpenCV REQUIRED)

# project itself
include_directories(${PROJECT_SOURCE_DIR}/include)
aux_source_directory(source CLASSES_FILES)

add_executable(${PROJECT_NAME}
    main.cpp
    ${CLASSES_FILES}
)
target_link_libraries(${PROJECT_NAME}
    libv4l2cpp
    ${OpenCV_LIBS}
)