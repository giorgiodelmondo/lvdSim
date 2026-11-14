# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(bench_wave bench_wave.cpp)
target_link_libraries(bench_wave PRIVATE lvd::wave)
lvd_target_warnings(bench_wave)

add_executable(bench_display_edit bench_display_edit.cpp)
target_link_libraries(bench_display_edit PRIVATE lvd::wave)
lvd_target_warnings(bench_display_edit)

add_executable(bench_search bench_search.cpp)
target_link_libraries(bench_search PRIVATE lvd::wave)
lvd_target_warnings(bench_search)

add_executable(bench_expression bench_expression.cpp)
target_link_libraries(bench_expression PRIVATE lvd::wave)
lvd_target_warnings(bench_expression)

add_executable(bench_memory bench_memory.cpp)
target_link_libraries(bench_memory PRIVATE lvd::wave)
lvd_target_warnings(bench_memory)
