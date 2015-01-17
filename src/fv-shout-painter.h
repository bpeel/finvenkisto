/*
 * Finvenkisto
 *
 * Copyright (C) 2015 Neil Roberts
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

#ifndef FV_SHOUT_PAINTER_H
#define FV_SHOUT_PAINTER_H

#include "fv-logic.h"
#include "fv-shader-data.h"
#include "fv-paint-state.h"

struct fv_shout_painter *
fv_shout_painter_new(struct fv_shader_data *shader_data);

void
fv_shout_painter_paint(struct fv_shout_painter *painter,
                       struct fv_logic *logic,
                       const struct fv_paint_state *paint_state);

void
fv_shout_painter_free(struct fv_shout_painter *painter);

#endif /* FV_SHOUT_PAINTER_H */
