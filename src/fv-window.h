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

#ifndef FV_WINDOW_H
#define FV_WINDOW_H

#include <stdbool.h>
#include "fv-vk.h"
#include "fv-vk-data.h"

struct fv_window *
fv_window_new(bool is_fullscreen);

void
fv_window_resized(struct fv_window *window);

void
fv_window_toggle_fullscreen(struct fv_window *window);

void
fv_window_get_extent(struct fv_window *window,
                     VkExtent2D *extent);

struct fv_vk_data *
fv_window_get_vk_data(struct fv_window *window);

bool
fv_window_begin_paint(struct fv_window *window,
                      bool need_clear);

bool
fv_window_end_paint(struct fv_window *window);

void
fv_window_free(struct fv_window *window);

#endif /* FV_WINDOW_H */
