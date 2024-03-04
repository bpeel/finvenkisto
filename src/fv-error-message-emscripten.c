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

#include <SDL.h>
#include <stdarg.h>
#include <emscripten.h>

#include "fv-error-message.h"
#include "fv-buffer.h"

void
fv_error_message(const char *format, ...)
{
        struct fv_buffer buffer;
        va_list ap;

        /* Stop drawing or processing any further events */
        SDL_SetEventFilter(NULL, NULL);
        emscripten_cancel_main_loop();

        va_start(ap, format);

        fv_buffer_init(&buffer);
        fv_buffer_append_vprintf(&buffer, format, ap);

        va_end(ap);

        EM_ASM_({
                        console.error(Module.UTF8ToString($0));
                        var canvas = document.getElementById("canvas");
                        canvas.style.display = "none";
                        var errorMessage =
                                document.getElementById("error-message");
                        errorMessage.style.display = "block";
                },
                buffer.data);

        fv_buffer_destroy(&buffer);
}
