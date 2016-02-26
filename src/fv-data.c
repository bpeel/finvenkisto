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

#include "fv-data.h"
#include "fv-util.h"

static char *
base_path;

void
fv_data_init(const char *exe_name)
{
        int len = strlen(exe_name);
        int i;

        for (i = len - 1; i >= 0; i--) {
                if (exe_name[i] == FV_PATH_SEPARATOR[0]) {
                        base_path = fv_alloc(i + 1);
                        memcpy(base_path, exe_name, i);
                        base_path[i] = '\0';
                        return;
                }
        }

        base_path = fv_strdup(".");
}

char *
fv_data_get_filename(const char *name)
{
        return fv_strconcat(base_path,
                            FV_PATH_SEPARATOR "data"
                            FV_PATH_SEPARATOR,
                            name,
                            NULL);
}

void
fv_data_deinit(void)
{
        fv_free(base_path);
}
