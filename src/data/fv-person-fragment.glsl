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

#if defined(HAVE_INSTANCED_ARRAYS) && defined(HAVE_TEXTURE_2D_ARRAY)
varying vec3 tex_coord;
uniform sampler2DArray tex;
#define textureFunc texture2DArray
#else
varying vec2 tex_coord;
uniform sampler2D tex;
#define textureFunc texture2D
#endif

varying vec2 tint;

void
main()
{
        gl_FragColor = vec4(mix(textureFunc(tex, tex_coord).rgb,
                                vec3(0.0, 1.0, 0.0),
                                tint.x) * tint.y, 1.0);
}
