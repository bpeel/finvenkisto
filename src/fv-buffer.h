/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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

#ifndef FV_BUFFER_H
#define FV_BUFFER_H

#include <stdint.h>
#include <stdarg.h>

#include "fv-util.h"

struct fv_buffer {
        uint8_t *data;
        size_t length;
        size_t size;
};

#define FV_BUFFER_STATIC_INIT { .data = NULL, .length = 0, .size = 0 }

void
fv_buffer_init(struct fv_buffer *buffer);

void
fv_buffer_ensure_size(struct fv_buffer *buffer,
                      size_t size);

void
fv_buffer_set_length(struct fv_buffer *buffer,
                     size_t length);

FV_PRINTF_FORMAT(2, 3) void
fv_buffer_append_printf(struct fv_buffer *buffer,
                        const char *format, ...);

void
fv_buffer_append_vprintf(struct fv_buffer *buffer,
                         const char *format,
                         va_list ap);

void
fv_buffer_append(struct fv_buffer *buffer,
                 const void *data,
                 size_t length);

static inline void
fv_buffer_append_c(struct fv_buffer *buffer,
                   char c)
{
        if (buffer->size > buffer->length)
                buffer->data[buffer->length++] = c;
        else
                fv_buffer_append(buffer, &c, 1);
}

void
fv_buffer_append_string(struct fv_buffer *buffer,
                        const char *str);

void
fv_buffer_destroy(struct fv_buffer *buffer);

#endif /* FV_BUFFER_H */
