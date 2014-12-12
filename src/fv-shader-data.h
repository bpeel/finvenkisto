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

#ifndef FV_SHADER_DATA_H
#define FV_SHADER_DATA_H

#include <epoxy/gl.h>
#include <stdbool.h>

enum fv_shader_data_program {
        FV_SHADER_DATA_PROGRAM_SIMPLE,
        FV_SHADER_DATA_PROGRAM_MAP,
        FV_SHADER_DATA_N_PROGRAMS
};

struct fv_shader_data {
        GLuint programs[FV_SHADER_DATA_N_PROGRAMS];
};

bool
fv_shader_data_init(struct fv_shader_data *data);

void
fv_shader_data_destroy(struct fv_shader_data *data);

#endif /* FV_SHADER_DATA_H */
