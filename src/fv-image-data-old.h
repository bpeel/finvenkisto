/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015 Neil Roberts
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

#ifndef FV_IMAGE_H
#define FV_IMAGE_H

#include <stdint.h>
#include <GL/gl.h>
#include "fv-image-data.h"

enum fv_image_data_old_image {
/*#include "data/fv-image-data-enum.h"*/
        FV_IMAGE_DATA_STUB
};

struct fv_image_data_old;

struct fv_image_data_old *
fv_image_data_old_new(void);

void
fv_image_data_old_get_size(struct fv_image_data_old *data,
                       enum fv_image_data_old_image image,
                       int *width,
                       int *height);

void
fv_image_data_old_set_2d(struct fv_image_data_old *data,
                     GLenum target,
                     GLint level,
                     GLint internal_format,
                     enum fv_image_data_old_image image);

void
fv_image_data_old_set_sub_2d(struct fv_image_data_old *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset,
                         enum fv_image_data_old_image image);

void
fv_image_data_old_set_sub_3d(struct fv_image_data_old *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset, GLint z_offset,
                         enum fv_image_data_old_image image);

void
fv_image_data_old_free(struct fv_image_data_old *data);

#endif /* FV_IMAGE_H */
