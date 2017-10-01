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

#include "config.h"

#include <dlfcn.h>

#include "fv-vk.h"
#include "fv-error-message.h"
#include "fv-util.h"

struct fv_vk fv_vk;

static void *lib_vulkan;

struct function {
        const char *name;
        size_t offset;
};

#define FV_VK_FUNC(func_name) \
        { .name = #func_name, .offset = offsetof(struct fv_vk, func_name) },

static const struct function
core_functions[] = {
#include "fv-vk-core-funcs.h"
};

static const struct function
instance_functions[] = {
#include "fv-vk-instance-funcs.h"
};

static const struct function
device_functions[] = {
#include "fv-vk-device-funcs.h"
};

#undef FV_VK_FUNC

typedef void *
(* func_getter)(void *object,
                const char *func_name);

static void
init_functions(func_getter getter,
               void *object,
               const struct function *functions,
               int n_functions)
{
        const struct function *function;
        int i;

        for (i = 0; i < n_functions; i++) {
                function = functions + i;
                *(void **) ((char *) &fv_vk + function->offset) =
                        getter(object, function->name);
        }
}

bool
fv_vk_load_libvulkan(void)
{
        lib_vulkan = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_GLOBAL);

        if (lib_vulkan == NULL) {
                fv_error_message("Error openining libvulkan.so.1: %s",
                                 dlerror());
                return false;
        }

        fv_vk.vkGetInstanceProcAddr =
                dlsym(lib_vulkan, "vkGetInstanceProcAddr");

        init_functions((func_getter) fv_vk.vkGetInstanceProcAddr,
                       NULL, /* object */
                       core_functions,
                       FV_N_ELEMENTS(core_functions));

        return true;
}

void
fv_vk_init_instance(VkInstance instance)
{
        init_functions((func_getter) fv_vk.vkGetInstanceProcAddr,
                       instance,
                       instance_functions,
                       FV_N_ELEMENTS(instance_functions));
}

void
fv_vk_init_device(VkDevice device)
{
        init_functions((func_getter) fv_vk.vkGetDeviceProcAddr,
                       device,
                       device_functions,
                       FV_N_ELEMENTS(device_functions));
}

void
fv_vk_unload_libvulkan(void)
{
        dlclose(lib_vulkan);
}
