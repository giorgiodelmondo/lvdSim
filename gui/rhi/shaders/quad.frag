// SPDX-License-Identifier: GPL-3.0-or-later
#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D atlas;

void main()
{
    fragColor = texture(atlas, v_uv);   // tiles are stored premultiplied
}
