# SPDX-License-Identifier: GPL-3.0-or-later
add_library(lvdtools STATIC
    src/process.cpp
    src/library_map.cpp
    src/translate.cpp
    src/environment.cpp
    src/libraries.cpp
    src/vendor_libs.cpp
    src/coverage.cpp
    src/project.cpp
    src/hdl_scanner.cpp
    src/compile_plan.cpp
    src/compile_runner.cpp
    src/diagnostics.cpp
    src/do_import.cpp
    src/regression.cpp
    src/project_cli.cpp)
target_include_directories(lvdtools PUBLIC src)
target_compile_features(lvdtools PUBLIC cxx_std_20)
target_compile_definitions(lvdtools PRIVATE LVDSIM_SOURCE_TCL_DIR="${CMAKE_SOURCE_DIR}/tcl/compat")
lvd_target_warnings(lvdtools)

add_executable(lvdsim-cli src/main.cpp)
target_link_libraries(lvdsim-cli PRIVATE lvdtools)
lvd_target_warnings(lvdsim-cli)

# The command names: copies of the multi-call binary next to it.
set(LVD_TOOL_NAMES vlib vmap vcom vlog vsim)
foreach(tool ${LVD_TOOL_NAMES})
    add_custom_command(TARGET lvdsim-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:lvdsim-cli> $<TARGET_FILE_DIR:lvdsim-cli>/${tool}${CMAKE_EXECUTABLE_SUFFIX}
        VERBATIM)
endforeach()

if(LVDSIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
