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

#include "config.h"

#include <alloca.h>

#include "fv-allocate-image-store.h"
#include "fv-util.h"

VkResult
fv_allocate_image_store(const struct fv_vk_data *vk_data,
                        uint32_t memory_type_flags,
                        int n_images,
                        const VkImage *images,
                        VkDeviceMemory *memory_out,
                        int *memory_type_index_out)
{
        VkDeviceMemory memory;
        VkMemoryRequirements reqs;
        VkResult res;
        int offset = 0;
        int *offsets = alloca(sizeof *offsets * n_images);
        int memory_type_index;
        uint32_t usable_memory_types = UINT32_MAX;
        VkDeviceSize granularity;
        int i;

        granularity = vk_data->device_properties.limits.bufferImageGranularity;

        for (i = 0; i < n_images; i++) {
                fv_vk.vkGetImageMemoryRequirements(vk_data->device,
                                                   images[i],
                                                   &reqs);
                offset = fv_align(offset, granularity);
                offset = fv_align(offset, reqs.alignment);
                offsets[i] = offset;
                offset += reqs.size;

                usable_memory_types &= reqs.memoryTypeBits;
        }

        while (usable_memory_types) {
                i = fv_util_ffs(usable_memory_types) - 1;

                if ((vk_data->memory_properties.memoryTypes[i].propertyFlags &
                     memory_type_flags) == memory_type_flags) {
                        memory_type_index = i;
                        goto found_memory_type;
                }

                usable_memory_types &= ~(1 << i);
        }

        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
found_memory_type: (void) 0;

        VkMemoryAllocateInfo allocate_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = offset,
                .memoryTypeIndex = memory_type_index
        };
        res = fv_vk.vkAllocateMemory(vk_data->device,
                                     &allocate_info,
                                     NULL, /* allocator */
                                     &memory);
        if (res != VK_SUCCESS)
                return res;

        for (i = 0; i < n_images; i++) {
                fv_vk.vkBindImageMemory(vk_data->device,
                                        images[i],
                                        memory,
                                        offsets[i]);
        }

        *memory_out = memory;
        if (memory_type_index_out)
                *memory_type_index_out = memory_type_index;

        return VK_SUCCESS;
}
