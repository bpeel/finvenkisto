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

layout(triangles, equal_spacing, cw) in;

layout(location = 0) in vec2 position[];
layout(location = 1) patch in vec3 circle_position_radius;
layout(location = 0) out vec4 color;

void main()
{
        vec2 p0 = gl_TessCoord.x * position[0];
        vec2 p1 = gl_TessCoord.y * position[1];
        vec2 p2 = gl_TessCoord.z * position[2];
        vec2 in_circle_pos = normalize(p0 + p1 + p2);
        float radius = circle_position_radius.z;
        vec2 circle_pos = circle_position_radius.xy;

        gl_Position = vec4(in_circle_pos * radius + circle_pos,
                           0.0,
                           1.0);
        color = vec4(1.0);
}
