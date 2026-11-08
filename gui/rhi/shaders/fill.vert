// SPDX-License-Identifier: GPL-3.0-or-later
#version 440

layout(location = 0) in vec2 position;   // pixels, origin top-left
layout(location = 1) in vec4 color;      // premultiplied alpha

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform Params {
    mat4 clip;    // QRhi::clipSpaceCorrMatrix()
    vec2 size;    // target size in pixels
} params;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    v_color = color;
    vec2 ndc = vec2(position.x / params.size.x * 2.0 - 1.0, 1.0 - position.y / params.size.y * 2.0);
    gl_Position = params.clip * vec4(ndc, 0.0, 1.0);
}
