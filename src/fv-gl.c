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

#include "config.h"

#include <GL/gl.h>
#include <SDL.h>

#include "fv-gl.h"
#include "fv-util.h"

struct fv_gl fv_gl;

static const char *
gl_funcs[] = {
#define FV_GL_FUNC(return_type, name, args) #name,
#include "fv-gl-funcs.h"
#undef FV_GL_FUNC
};

void
fv_gl_init(void)
{
        void **ptrs = (void **) &fv_gl;
        int i;

        for (i = 0; i < FV_N_ELEMENTS(gl_funcs); i++)
                ptrs[i] = SDL_GL_GetProcAddress(gl_funcs[i]);

        fv_gl.have_vertex_array_objects = true;

        fv_gl.have_instanced_arrays =
                SDL_GL_ExtensionSupported("GL_ARB_instanced_arrays");
}
