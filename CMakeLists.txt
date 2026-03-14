cmake_minimum_required(VERSION 3.16)

project(qt-melodica LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

add_executable(qt-melodica
    src/main.cpp
    src/MelodicaWindow.h
    src/MelodicaWindow.cpp
    src/HingeSensor.h
    src/HingeSensor.cpp
    src/AudioEngine.h
    src/AudioEngine.cpp
    src/SynthVoice.h
    src/SynthVoice.cpp
    src/KeyMap.h
)

target_link_libraries(qt-melodica PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
)

if (UNIX AND NOT APPLE)
    target_link_libraries(qt-melodica PRIVATE m)
endif()
