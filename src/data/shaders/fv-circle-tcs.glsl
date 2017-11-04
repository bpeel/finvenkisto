#version 420 core

/*
 * Finvenkisto
 *
 * Copyright (C) 2017 Neil Roberts
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

layout(vertices = 3) out;

layout(location = 0) in vec2 position_in[];
layout(location = 1) in vec3 circle_position_radius_in[];

layout(location = 0) out vec2 position_out[];
layout(location = 1) patch out vec3 circle_position_radius_out;

const float MAX_TESSELLATION = 16.0;

void
main()
{
        position_out[gl_InvocationID] =
                position_in[gl_InvocationID];

        if (gl_InvocationID == 0) {
                gl_TessLevelInner[0] = 0.0;
                circle_position_radius_out = circle_position_radius_in[0];

                float radius = circle_position_radius_in[gl_InvocationID].z;
                float tessellation = radius * MAX_TESSELLATION;
                gl_TessLevelOuter[0] = tessellation;
                gl_TessLevelOuter[1] = tessellation;
                gl_TessLevelOuter[2] = tessellation;
        }
}
