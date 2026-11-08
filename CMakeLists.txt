# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(lvdsim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# All programs end up in one directory so that vsim finds lvdsim-gui and the tools find each other.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(LVDSIM_BUILD_GUI "Build the Qt GUI (needs Qt 6)" OFF)
option(LVDSIM_BUILD_TOOLS "Build the vlib/vcom/vlog/vsim wrapper binary" ON)
option(LVDSIM_BUILD_TESTS "Build unit tests" ON)
option(LVDSIM_BUILD_BENCH "Build benchmarks" OFF)
option(LVDSIM_WARNINGS_AS_ERRORS "Treat warnings in lvdsim code as errors" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(LvdWarnings)
include(LvdDependencies)

if(LVDSIM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(third_party/fstapi)
add_subdirectory(libwave)

if(LVDSIM_BUILD_TOOLS AND EXISTS "${CMAKE_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()
if(LVDSIM_BUILD_GUI AND EXISTS "${CMAKE_SOURCE_DIR}/gui/CMakeLists.txt")
    add_subdirectory(gui)
endif()
