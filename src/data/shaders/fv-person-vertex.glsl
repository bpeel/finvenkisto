#version 420 core

/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015, 2017 Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "fv-lighting.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex_coord_attrib;
layout(location = 2) in vec3 normal_attrib;

layout(location = 3) in mat4 transform;
layout(location = 7) in mat3 normal_transform;
layout(location = 10) in float tex_layer;
layout(location = 11) in float green_tint_attrib;

layout(location = 0) out vec3 tex_coord;
layout(location = 1) out vec2 tint;

void
main()
{
        gl_Position = transform * vec4(position, 1.0);

        tex_coord = vec3(tex_coord_attrib, tex_layer);

        tint = vec2(green_tint_attrib,
                    get_lighting_tint(normal_transform, normal_attrib));
}
