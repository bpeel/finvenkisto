/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2017 Neil Roberts
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

#ifndef FV_MODEL_H
#define FV_MODEL_H

#include <stdbool.h>
#include "fv-vk-data.h"

enum fv_model_type {
        FV_MODEL_TYPE_COLOR,
        FV_MODEL_TYPE_TEXTURE
};

struct fv_model {
        enum fv_model_type type;
        VkBuffer buffer;
        VkDeviceMemory memory;
        size_t indices_offset;
        size_t vertices_offset;
        int n_vertices;
        int n_indices;
};

bool
fv_model_load(const struct fv_vk_data *vk_data,
              struct fv_model *model,
              const char *filename);

void
fv_model_paint(const struct fv_model *model);

void
fv_model_destroy(const struct fv_vk_data *vk_data,
                 struct fv_model *model);

#endif /* FV_MODEL_H */
