# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(libwave_tests
    test_version.cpp test_fstapi_roundtrip.cpp test_store.cpp test_radix.cpp test_lod.cpp
    test_display_model.cpp test_display_edit.cpp test_fst_loader.cpp test_live_reader.cpp test_wave_commands.cpp test_search.cpp test_list_index.cpp test_expression.cpp test_compare.cpp test_expression_rows.cpp test_memory.cpp)
target_link_libraries(libwave_tests PRIVATE lvd::wave Catch2::Catch2WithMain)
lvd_target_warnings(libwave_tests)

include(Catch)
catch_discover_tests(libwave_tests)
set_source_files_properties(test_version.cpp PROPERTIES COMPILE_DEFINITIONS LVD_EXPECTED_VERSION="${PROJECT_VERSION}")
