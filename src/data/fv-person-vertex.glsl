/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015 Neil Roberts
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

layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 tex_coord_attrib;
layout(location = 4) in mat4 transform;
layout(location = 8) in float tex_layer;
layout(location = 9) in float green_tint_attrib;

out vec3 tex_coord;
flat out float green_tint;

void
main()
{
        gl_Position = transform * vec4(pos, 1.0);
        tex_coord = vec3(tex_coord_attrib, tex_layer);
        green_tint = green_tint_attrib;
}

