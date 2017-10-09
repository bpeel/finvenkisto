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

#ifndef FV_HIGHLIGHT_PAINTER_H
#define FV_HIGHLIGHT_PAINTER_H

#include "fv-pipeline-data.h"
#include "fv-paint-state.h"
#include "fv-vk-data.h"

struct fv_highlight_painter_highlight {
        float x, y;
        float w, h;
        float z;
        uint8_t r, g, b, a;
};

struct fv_highlight_painter *
fv_highlight_painter_new(const struct fv_vk_data *vk_data,
                         const struct fv_pipeline_data *pipeline_data);

void
fv_highlight_painter_begin_frame(struct fv_highlight_painter *painter);

void
fv_highlight_painter_paint(struct fv_highlight_painter *painter,
                           VkCommandBuffer command_buffer,
                           size_t n_highlights,
                           const struct fv_highlight_painter_highlight *
                           highlights,
                           struct fv_paint_state *paint_state);

void
fv_highlight_painter_end_frame(struct fv_highlight_painter *painter);

void
fv_highlight_painter_free(struct fv_highlight_painter *painter);

#endif /* FV_HIGHLIGHT_PAINTER_H */
