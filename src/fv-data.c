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

#include <SDL.h>

#include "fv-data.h"
#include "fv-util.h"

#ifdef WIN32
#define FV_DATA_SEPARATOR "\\"
#else
#define FV_DATA_SEPARATOR "/"
#endif

char *
fv_data_get_filename(const char *name)
{
#ifdef EMSCRIPTEN
        return fv_strdup(name);
#else
        char *data_path = SDL_GetBasePath();
        char *full_path;

        if (data_path == NULL)
                return NULL;

        full_path = fv_strconcat(data_path,
                                 FV_DATA_SEPARATOR "data"
                                 FV_DATA_SEPARATOR,
                                 name,
                                 NULL);

        SDL_free(data_path);

        return full_path;
#endif /* EMSCRIPTEN */
}
