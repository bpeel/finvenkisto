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

#include "config.h"

#include "fv-flush-memory.h"

VkResult
fv_flush_memory(const struct fv_vk_data *vk_data,
                int memory_type_index,
                VkDeviceMemory memory,
                VkDeviceSize size)
{
        const VkMemoryType *memory_type =
                &vk_data->memory_properties.memoryTypes[memory_type_index];

        /* We don’t need to do anything if the memory is already
         * coherent */
        if ((memory_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return VK_SUCCESS;

        VkMappedMemoryRange mapped_memory_range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = memory,
                .offset = 0,
                .size = size
        };
        return fv_vk.vkFlushMappedMemoryRanges(vk_data->device,
                                               1, /* memoryRangeCount */
                                               &mapped_memory_range);
}
