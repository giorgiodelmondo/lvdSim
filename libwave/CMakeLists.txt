# SPDX-License-Identifier: GPL-3.0-or-later
add_library(lvdwave STATIC
    src/version.cpp src/logic.cpp src/store.cpp src/fst_loader.cpp src/radix.cpp src/lod.cpp src/display_model.cpp src/display_edit.cpp src/live_reader.cpp src/wave_commands.cpp)
target_include_directories(lvdwave PUBLIC include)
target_link_libraries(lvdwave PUBLIC lvd::fstapi)
target_compile_features(lvdwave PUBLIC cxx_std_20)
set_source_files_properties(src/version.cpp PROPERTIES COMPILE_DEFINITIONS LVD_VERSION_STRING="${PROJECT_VERSION}")
lvd_target_warnings(lvdwave)
add_library(lvd::wave ALIAS lvdwave)

if(LVDSIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(LVDSIM_BUILD_BENCH)
    add_subdirectory(bench)
endif()
