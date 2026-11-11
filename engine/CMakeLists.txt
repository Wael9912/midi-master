# SPDX-License-Identifier: GPL-3.0-only
# Midi Master engine. The core has no Android dependency: it builds and is tested on the desktop
# (cmake -S engine -B build/engine && cmake --build build/engine && ctest --test-dir build/engine).
cmake_minimum_required(VERSION 3.22)
project(midimaster_engine CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mm_core STATIC core/synth.cpp)
target_include_directories(mm_core PUBLIC core)
if(NOT MSVC)
  target_compile_options(mm_core PRIVATE -Wall -Wextra -Werror)
endif()

option(MM_BUILD_TESTS "Build the desktop tests" ON)
if(MM_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  add_executable(mm_tests tests/test_main.cpp)
  target_link_libraries(mm_tests PRIVATE mm_core)
  add_test(NAME mm_tests COMMAND mm_tests)
endif()
