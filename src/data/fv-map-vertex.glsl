/*
 * Finvenkisto
 *
 * Copyright (C) 2014 Neil Roberts
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

attribute vec4 position;
attribute vec2 tex_coord_attrib;

uniform mat4 transform;
uniform mat3 normal_transform;

varying vec2 tex_coord;
varying float tint;

float
get_lighting_tint(mat3 normal_transform,
                  vec3 normal);

void
main()
{
        vec3 normal;

        if (position[3] < 1.0) {
                normal = vec3(0.0, 0.0, 1.0);
        } else {
                float normal_index = position[3] - 128.0f;
                float normal_sign = sign(normal_index);

                normal.z = 0.0;

                if (abs(normal_index) > 100.0)
                        normal.xy = vec2(normal_sign, 0.0);
                else
                        normal.xy = vec2(0.0, normal_sign);
        }

        tint = get_lighting_tint(normal_transform, normal);

        gl_Position = transform * vec4(position.xyz, 1.0);
        tex_coord = tex_coord_attrib;
}

