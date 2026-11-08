# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(tools_tests test_library_map.cpp test_translate.cpp test_process.cpp test_libraries.cpp test_environment.cpp test_vendor_libs.cpp)
target_link_libraries(tools_tests PRIVATE lvdtools Catch2::Catch2WithMain)
lvd_target_warnings(tools_tests)

# Integration test: drives the real lvdsim-cli and a real nvc. Skipped when nvc cannot be found.
# Override the default (the kernel build tree) with LVDSIM_NVC / LVDSIM_NVC_LIBDIR / LVDSIM_NVC_DLLDIR.
add_executable(tools_integration_tests test_integration.cpp)
target_link_libraries(tools_integration_tests PRIVATE lvdtools Catch2::Catch2WithMain)
target_compile_definitions(tools_integration_tests PRIVATE
    LVD_CLI_EXE="$<TARGET_FILE:lvdsim-cli>"
    LVD_KERNEL_BUILD="${CMAKE_SOURCE_DIR}/kernel/nvc/build")
lvd_target_warnings(tools_integration_tests)
add_dependencies(tools_integration_tests lvdsim-cli)

include(Catch)
catch_discover_tests(tools_tests)
catch_discover_tests(tools_integration_tests PROPERTIES TIMEOUT 120)

# scripts/libs/compile_libs.sh against a made-up Vivado tree (skips with code 77 without nvc or the tools).
find_program(LVD_SH sh HINTS "C:/Program Files/Git/usr/bin" "C:/msys64/usr/bin")
if(LVD_SH)
    add_test(NAME scripts_libs_compile COMMAND ${LVD_SH}
        ${CMAKE_SOURCE_DIR}/scripts/libs/test_libs.sh $<TARGET_FILE_DIR:lvdsim-cli>)
    set_tests_properties(scripts_libs_compile PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
endif()
