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

attribute vec3 position;
attribute vec2 tex_coord_attrib;

#ifdef HAVE_INSTANCED_ARRAYS
attribute mat4 transform;
#else /* HAVE_INSTANCED_ARRAYS */
uniform mat4 transform;
#endif

varying vec2 tex_coord;

void
main()
{
        gl_Position = transform * vec4(position, 1.0);
        tex_coord = tex_coord_attrib;
}

