# SPDX-License-Identifier: GPL-3.0-or-later
if(NOT TARGET lvdtools)
    message(FATAL_ERROR "LVDSIM_BUILD_GUI needs LVDSIM_BUILD_TOOLS=ON")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

set(CMAKE_AUTOMOC ON)

add_library(lvdgui_wave STATIC
    wave/view_state.cpp
    wave/row_source.cpp
    wave/tile_geometry.cpp
    wave/painter_renderer.cpp
    wave/tile_cache.cpp
    wave/render_policy.cpp
    wave/wave_widget.cpp
    wave/wave_widget_arrange.cpp
    wave/wave_toolbar.cpp
    wave/icons.cpp)
target_include_directories(lvdgui_wave PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvdgui_wave PUBLIC lvd::wave Qt6::Core Qt6::Gui Qt6::Widgets)
include(rhi/rhi.cmake)
lvd_add_rhi(lvdgui_wave)
lvd_target_warnings(lvdgui_wave)

add_library(lvdgui_app STATIC
    sim/output_filter.cpp
    sim/nvc_session.cpp
    sim/parsers/json_replies.cpp
    sim/parsers/report_lines.cpp
    app/transcript_widget.cpp
    app/objects_pane.cpp
    app/library_pane.cpp
    app/structure_pane.cpp
    app/processes_pane.cpp
    app/breakpoint_pane.cpp
    app/source_pane.cpp
    app/compile_libs_dialog.cpp
    app/portable_mode.cpp
    app/vhdl_highlighter.cpp
    app/sim_window.cpp)
target_include_directories(lvdgui_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvdgui_app PUBLIC lvdgui_wave lvdtools)
lvd_target_warnings(lvdgui_app)

add_executable(lvdsim-gui WIN32 app/gui_main.cpp)
target_link_libraries(lvdsim-gui PRIVATE lvdgui_app)
lvd_target_warnings(lvdsim-gui)

add_executable(lvdsim-wave-demo WIN32 app/main.cpp)
target_link_libraries(lvdsim-wave-demo PRIVATE lvdgui_wave)
lvd_target_warnings(lvdsim-wave-demo)

if(LVDSIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(LVDSIM_BUILD_BENCH)
    add_executable(bench_paint bench/bench_paint.cpp)
    target_link_libraries(bench_paint PRIVATE lvdgui_wave)
    lvd_target_warnings(bench_paint)
endif()
