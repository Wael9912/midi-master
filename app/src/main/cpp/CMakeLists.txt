# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.22)
project(midimaster CXX)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine)
set(MM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${ENGINE_DIR} engine)

find_package(oboe REQUIRED CONFIG)

add_library(midimaster SHARED
    ${ENGINE_DIR}/android/audio_engine.cpp
    ${ENGINE_DIR}/android/jni_bridge.cpp)
target_include_directories(midimaster PRIVATE ${ENGINE_DIR}/android)
target_compile_options(midimaster PRIVATE -Wall -Wextra -Werror)
target_link_libraries(midimaster PRIVATE mm_core oboe::oboe amidi log android)
# 16 KB page size support (required on new Android devices)
target_link_options(midimaster PRIVATE "-Wl,-z,max-page-size=16384")
