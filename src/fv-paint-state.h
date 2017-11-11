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

#ifndef FV_PAINT_STATE_H
#define FV_PAINT_STATE_H

#include "fv-transform.h"

struct fv_paint_state {
        int viewport_x, viewport_y;
        int viewport_width, viewport_height;
        struct fv_transform transform;
        float center_x, center_y;
        float visible_w, visible_h;
};

#endif /* FV_PAINT_STATE_H */
