#version 430 core

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

layout(location = 0) in vec3 tex_coord;
layout(location = 1) in vec2 tint;

layout(location = 0) out vec4 frag_color;
layout(binding = 0) uniform sampler2DArray tex;


void
main()
{
        frag_color = vec4(mix(texture(tex, tex_coord).rgb,
                              vec3(0.0, 1.0, 0.0),
                              tint.x) * tint.y, 1.0);
}
