/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014, 2017 Neil Roberts
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

#ifndef FV_PERSON_PAINTER_H
#define FV_PERSON_PAINTER_H

#include "fv-logic.h"
#include "fv-image-data.h"
#include "fv-pipeline-data.h"
#include "fv-vk-data.h"
#include "fv-paint-state.h"

struct fv_person_painter *
fv_person_painter_new(const struct fv_vk_data *vk_data,
                      const struct fv_pipeline_data *pipeline_data,
                      const struct fv_image_data *image_data);

void
fv_person_painter_paint(struct fv_person_painter *painter,
                        struct fv_logic *logic,
                        VkCommandBuffer command_buffer,
                        const struct fv_paint_state *paint_state);

void
fv_person_painter_free(struct fv_person_painter *painter);

#endif /* FV_PERSON_PAINTER_H */
