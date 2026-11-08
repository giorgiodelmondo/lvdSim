// SPDX-License-Identifier: GPL-3.0-or-later
#version 440

layout(location = 0) in vec2 position;   // pixels, origin top-left
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform Params {
    mat4 clip;
    vec2 size;
} params;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    v_uv = uv;
    vec2 ndc = vec2(position.x / params.size.x * 2.0 - 1.0, 1.0 - position.y / params.size.y * 2.0);
    gl_Position = params.clip * vec4(ndc, 0.0, 1.0);
}
