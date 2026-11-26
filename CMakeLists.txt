cmake_minimum_required(VERSION 3.16)
project(B210RadioMapPost VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# automatically update software version
option(AUTO_VERSION "Automatically update software version" ON)
if(AUTO_VERSION)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
        @ONLY
    )
endif()

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets)

add_library(b210post_core
    src/BuildingShapes.hpp
    src/BuildingShapes.cpp
    src/CoordinateTransform.cpp
    src/CsvCodec.cpp
    src/RasterRenderer.cpp
    src/RasterCompletion.hpp
    src/RasterCompletion.cpp
    src/RasterInpainting.hpp
    src/RasterInpainting.cpp
)
if(AUTO_VERSION)
    target_compile_definitions(b210post_core PUBLIC AUTO_VERSION=1)
else()
    target_compile_definitions(b210post_core PUBLIC AUTO_VERSION=0)
endif()
target_include_directories(b210post_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    $<$<BOOL:${AUTO_VERSION}>:${CMAKE_CURRENT_BINARY_DIR}/generated>
)
target_link_libraries(b210post_core PUBLIC Qt5::Core Qt5::Gui)

add_executable(B210RadioMapPost
    src/main.cpp
    src/MainWindow.hpp
    src/MainWindow.cpp
    src/BuildingShapeDialog.hpp
    src/BuildingShapeDialog.cpp
    src/PreciseDoubleSpinBox.hpp
    src/PreciseDoubleSpinBox.cpp
    src/Interpolation.hpp
    src/Interpolation.cpp
    src/LocalFillDialog.hpp
    src/LocalFillDialog.cpp
    src/RasterPreviewDialog.hpp
    src/RasterPreviewDialog.cpp
    src/Models.hpp
    src/Models.cpp
    src/SpatialWidget.hpp
    src/SpatialWidget.cpp
    src/SettingsDialog.hpp
    src/SettingsDialog.cpp
    src/RawRecoveryDialog.hpp
    src/RawRecoveryDialog.cpp
)
target_include_directories(B210RadioMapPost PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(B210RadioMapPost PRIVATE b210post_core Qt5::Widgets)

# GUI subsystem executable: no console window is created when the app is launched (MinGW: -mwindows, MSVC: /SUBSYSTEM:WINDOWS)
if(WIN32)
    set_target_properties(B210RadioMapPost PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(MSVC)
    target_compile_options(b210post_core PRIVATE /W4 /permissive-)
    target_compile_options(B210RadioMapPost PRIVATE /W4 /permissive-)
else()
    target_compile_options(b210post_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(B210RadioMapPost PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(b210post_tests
        tests/test_core.cpp
        src/Models.hpp
        src/Models.cpp
    )
    target_link_libraries(b210post_tests PRIVATE b210post_core Qt5::Core)
    add_test(NAME b210post_tests COMMAND b210post_tests)
endif()
