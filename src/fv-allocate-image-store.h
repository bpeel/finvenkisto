/*
 * Finvenkisto
 *
 * Copyright (C) 2016, 2017 Neil Roberts
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

#ifndef FV_FV_ALLOCATE_IMAGE_STORE_H
#define FV_FV_ALLOCATE_IMAGE_STORE_H

#include "fv-vk-data.h"

VkResult
fv_allocate_image_store(const struct fv_vk_data *data,
                        uint32_t memory_type_flags,
                        int n_images,
                        const VkImage *images,
                        VkDeviceMemory *memory_out,
                        int *memory_type_index_out);

#endif /* FV_FV_ALLOCATE_IMAGE_STORE_H */
