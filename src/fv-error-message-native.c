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

#include "config.h"

#include <stdio.h>
#include <SDL.h>
#include <stdarg.h>

#ifndef WIN32
#include <unistd.h>
#endif

#include "fv-error-message.h"
#include "fv-buffer.h"

void
fv_error_message(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
#ifndef WIN32
        if (isatty(STDERR_FILENO)) {
                vfprintf(stderr, format, ap);
                fputc('\n', stderr);
        } else
#endif
        {
                struct fv_buffer buffer;

                fv_buffer_init(&buffer);
                fv_buffer_append_vprintf(&buffer, format, ap);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                         "Finvenkisto - eraro",
                                         (char *) buffer.data,
                                         NULL);
                fv_buffer_destroy(&buffer);
        }

        va_end(ap);
}
