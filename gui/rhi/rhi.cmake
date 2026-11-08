# SPDX-License-Identifier: GPL-3.0-or-later
# The GPU renderer is part of the wave library (the wave widget owns the GPU canvas), added with
#   lvd_add_rhi(<target>)
# It needs Qt's private RHI headers and compiles the shaders for every backend at build time, embedding them
# as resources (":/lvdsim/shaders/*.qsb").
# Qt 6.8 on Debian has no separate Qt6GuiPrivate package: Qt6::GuiPrivate comes with Qt6Gui there (optional component).
find_package(Qt6 REQUIRED COMPONENTS Gui ShaderTools Widgets)
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()

function(lvd_add_rhi target)
    set(dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    target_sources(${target} PRIVATE "${dir}/gpu_wave_renderer.cpp" "${dir}/rhi_wave_canvas.cpp")
    target_link_libraries(${target} PUBLIC Qt6::GuiPrivate)
    qt_add_shaders(${target} "lvdsim_shaders"
        PREFIX "/lvdsim"
        BASE "${dir}"
        FILES
            "${dir}/shaders/fill.vert"
            "${dir}/shaders/fill.frag"
            "${dir}/shaders/quad.vert"
            "${dir}/shaders/quad.frag")
endfunction()
