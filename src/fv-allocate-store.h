/*
 * Finvenkisto
 *
 * Copyright (C) 2017 Neil Roberts
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

#ifndef FV_ALLOCATE_STORE_H
#define FV_ALLOCATE_STORE_H

#include "fv-vk-data.h"

VkResult
fv_allocate_store_image(const struct fv_vk_data *data,
                        uint32_t memory_type_flags,
                        int n_images,
                        const VkImage *images,
                        VkDeviceMemory *memory_out,
                        int *memory_type_index_out);

VkResult
fv_allocate_store_buffer(const struct fv_vk_data *vk_data,
                         uint32_t memory_type_flags,
                         int n_buffers,
                         const VkBuffer *buffers,
                         VkDeviceMemory *memory_out,
                         int *offsets);

#endif /* FV_ALLOCATE_STORE_H */
