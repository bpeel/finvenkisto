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

#ifndef FV_VK_DATA_H
#define FV_VK_DATA_H

#include "fv-vk.h"

struct fv_vk_data {
        VkDevice device;
        VkPhysicalDevice physical_device;
        VkPhysicalDeviceMemoryProperties memory_properties;
        VkPhysicalDeviceProperties device_properties;
        VkDescriptorPool descriptor_pool;
        int queue_family;
};

#endif /* FV_VK_DATA_H */
