cmake_minimum_required(VERSION 3.16)

project(untitled VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc)
find_package(Threads REQUIRED)

set(MVS_ROOT "/opt/MVS" CACHE PATH "Hikrobot MVS SDK installation directory")
find_path(MVS_INCLUDE_DIR
    NAMES MvCameraControl.h
    PATHS "${MVS_ROOT}/include"
    NO_DEFAULT_PATH)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_MVS_ARCH_PATHS "${MVS_ROOT}/lib/aarch64" "${MVS_ROOT}/lib/arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_MVS_ARCH_PATHS "${MVS_ROOT}/lib/64" "${MVS_ROOT}/lib/x86_64")
else()
    set(_MVS_ARCH_PATHS "${MVS_ROOT}/lib/32" "${MVS_ROOT}/lib/armhf")
endif()

find_library(MVS_CAMERA_LIBRARY
    NAMES MvCameraControl
    PATHS ${_MVS_ARCH_PATHS} "${MVS_ROOT}/lib"
    NO_DEFAULT_PATH)

if(NOT MVS_INCLUDE_DIR OR NOT MVS_CAMERA_LIBRARY)
    message(FATAL_ERROR
        "Hikrobot MVS SDK not found. Install it under /opt/MVS or set MVS_ROOT.")
endif()

qt_add_executable(untitled
    main.cpp
    mainwindow.cpp
    mainwindow.h
    visionruntime.cpp
    visionruntime.h
    resources.qrc
    ../algorithm/src/aglo_detector.cpp
    ../algorithm/src/aglo_fast_detector.cpp
)

target_include_directories(untitled PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/algorithm/include"
    "${MVS_INCLUDE_DIR}"
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(untitled PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Threads::Threads
    ${OpenCV_LIBS}
    "${MVS_CAMERA_LIBRARY}"
)

target_compile_options(untitled PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang>>:-ffast-math>
)

get_filename_component(MVS_LIBRARY_DIR "${MVS_CAMERA_LIBRARY}" DIRECTORY)
set_target_properties(untitled PROPERTIES
    BUILD_RPATH "${MVS_LIBRARY_DIR}"
    INSTALL_RPATH "${MVS_LIBRARY_DIR}"
)

include(GNUInstallDirs)
install(TARGETS untitled RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
