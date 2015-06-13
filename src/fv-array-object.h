/*
 * Finvenkisto
 *
 * Copyright (C) 2015 Neil Roberts
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

#ifndef FV_ARRAY_OBJECT_H
#define FV_ARRAY_OBJECT_H

#include <GL/gl.h>

struct fv_array_object;

struct fv_array_object *
fv_array_object_new(void);

void
fv_array_object_set_attribute(struct fv_array_object *array,
                              GLuint index,
                              GLint size,
                              GLenum type,
                              GLboolean normalized,
                              GLsizei stride,
                              GLuint divisor,
                              GLuint buffer,
                              size_t buffer_offset);

/* Sets the element buffer for the array object. Note that this will
 * also end up binding the element buffer so that it can be
 * immediately filled with data.
 */
void
fv_array_object_set_element_buffer(struct fv_array_object *array,
                                   GLuint buffer);

void
fv_array_object_bind(struct fv_array_object *array);

void
fv_array_object_free(struct fv_array_object *array);

#endif /* FV_ARRAY_OBJECT_H */
