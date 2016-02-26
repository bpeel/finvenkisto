#version 420 core

/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2016 Neil Roberts
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

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 tex_coord_attrib;

layout(std140, set = 0, binding = 0) uniform block {
        uniform mat4 transform;
        uniform mat3 normal_transform;
};

layout(location = 0) out float tint;

void
main()
{
        vec3 normal;

        if (position[3] < 1.0) {
                normal = vec3(0.0, 0.0, 1.0);
        } else {
                float normal_index = position[3] - 128.0;
                float normal_sign = sign(normal_index);

                normal.z = 0.0;

                if (abs(normal_index) > 100.0)
                        normal.xy = vec2(normal_sign, 0.0);
                else
                        normal.xy = vec2(0.0, normal_sign);
        }

        tint = get_lighting_tint(normal_transform, normal);

        gl_Position = transform * vec4(position.xyz, 1.0);
}

