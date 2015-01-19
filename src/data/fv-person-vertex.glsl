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

in vec3 position;
in vec2 tex_coord_attrib;

#ifdef HAVE_INSTANCED_ARRAYS
in mat4 transform;
in float tex_layer;
in float green_tint_attrib;
#else /* HAVE_INSTANCED_ARRAYS */
uniform mat4 transform;
uniform float tex_layer;
uniform float green_tint_attrib;
#endif

out vec3 tex_coord;
flat out float green_tint;

void
main()
{
        gl_Position = transform * vec4(position, 1.0);
        tex_coord = vec3(tex_coord_attrib, tex_layer);
        green_tint = green_tint_attrib;
}

