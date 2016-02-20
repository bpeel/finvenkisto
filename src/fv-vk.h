/*
 * Finvenkisto
 *
 * Copyright (C) 2016 Neil Roberts
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

#ifndef FV_VK_H
#define FV_VK_H

#include <vulkan/vulkan.h>
#include <stdbool.h>

struct fv_vk {
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

#define FV_VK_FUNC(name) PFN_ ## name name;
#include "fv-vk-core-funcs.h"
#include "fv-vk-instance-funcs.h"
#include "fv-vk-device-funcs.h"
#undef FV_VK_FUNC
};

extern struct fv_vk fv_vk;

bool
fv_vk_load_libvulkan(void);

void
fv_vk_init_instance(VkInstance instance);

void
fv_vk_init_device(VkDevice device);

void
fv_vk_unload_libvulkan(void);

#endif /* FV_VK_H */
