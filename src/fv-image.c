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

#include "fv-image.h"
#include "fv-data.h"
#include "fv-util.h"

#define STB_IMAGE_IMPLEMENTATION 1
#include "stb_image.h"

uint8_t *
fv_image_load(const char *name,
              int *width,
              int *height,
              int components)
{
        char *filename = fv_data_get_filename(name);
        int comp_out;
        uint8_t *data;

        if (filename == NULL) {
                fprintf(stderr, "Failed to get filename for %s\n", name);
                return NULL;
        }

        data = stbi_load(filename, width, height, &comp_out, components);

        if (data == NULL) {
                fprintf(stderr,
                        "%s: %s\n",
                        filename,
                        stbi_failure_reason());
        }

        fv_free(filename);

        return data;
}
