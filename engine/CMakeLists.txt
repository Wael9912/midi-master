# SPDX-License-Identifier: GPL-3.0-only
# Midi Master engine. The core has no Android dependency: it builds and is tested on the desktop
# (cmake -S engine -B build/engine && cmake --build build/engine && build/engine/mm_tests).
cmake_minimum_required(VERSION 3.22)
project(midimaster_engine CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(mm_core STATIC
  core/arranger.cpp
  core/builtin.cpp
  core/chord.cpp
  core/json.cpp
  core/org_sampler.cpp
  core/orgset.cpp
  core/sequencer.cpp
  core/sf2.cpp
  core/sf2_synth.cpp
  core/sty.cpp
  core/styledsl.cpp)
target_include_directories(mm_core PUBLIC core)
if(NOT MSVC)
  target_compile_options(mm_core PRIVATE -Wall -Wextra -Werror)
endif()

option(MM_BUILD_TESTS "Build the desktop tests" ON)
option(MM_TESTS_ON_DEVICE "Also build the tests for Android, to run them on the phone with adb" OFF)
if(MM_BUILD_TESTS AND (NOT ANDROID OR MM_TESTS_ON_DEVICE))
  enable_testing()
  add_executable(mm_tests tests/test_main.cpp tests/test_fixtures.cpp)
  target_link_libraries(mm_tests PRIVATE mm_core)
  target_compile_definitions(mm_tests PRIVATE
    MM_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    MM_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/assets"
    MM_LOCAL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../build/fixtures-local")
  add_test(NAME mm_tests COMMAND mm_tests)
endif()
