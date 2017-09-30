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

#ifndef FV_IMAGE_DATA_H
#define FV_IMAGE_DATA_H

#include <stdint.h>

#include "fv-vk-data.h"

enum fv_image_data_image {
#include "data/fv-image-data-enum.h"
};

struct fv_image_data;

struct fv_image_data *
fv_image_data_new(const struct fv_vk_data *vk_data,
                  VkCommandBuffer command_buffer);

void
fv_image_data_get_size(const struct fv_image_data *data,
                       enum fv_image_data_image image,
                       int *width,
                       int *height);

int
fv_image_data_get_miplevels(const struct fv_image_data *data,
                            enum fv_image_data_image image);

VkFormat
fv_image_data_get_format(const struct fv_image_data *data,
                         enum fv_image_data_image image);

VkResult
fv_image_data_create_image_2d(const struct fv_image_data *data,
                              enum fv_image_data_image image_num,
                              VkImage *image_out,
                              VkDeviceMemory *memory_out);

VkResult
fv_image_data_create_image_2d_array(const struct fv_image_data *data,
                                    int n_images,
                                    const enum fv_image_data_image *image_nums,
                                    VkImage *image_out,
                                    VkDeviceMemory *memory_out);

void
fv_image_data_free(struct fv_image_data *data);

#endif /* FV_IMAGE_DATA_H */
