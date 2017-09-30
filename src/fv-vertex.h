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

#ifndef FV_VERTEX_H
#define FV_VERTEX_H

#include <stdint.h>

struct fv_vertex_map {
        uint8_t x, y, z;
        /* The normal is encoded as the fourth component of the
         * position rather than its own component because I read
         * somewhere that all attributes should be aligned to a float.
         * I'm not sure if this is true or not but it's not really
         * difficult to do so we might as well play it safe.
         */
        uint8_t normal;
        uint16_t s, t;
};

struct fv_vertex_map_push_constants {
        float transform[16];
        float normal_transform[4 * 3];
};

struct fv_vertex_model_color {
        float x, y, z;
        float nx, ny, nz;
        uint8_t r, g, b;
};

struct fv_vertex_model_texture {
        float x, y, z;
        float nx, ny, nz;
        float s, t;
};

struct fv_instance_special {
        float modelview[4 * 4];
        float normal_transform[3 * 3];
};

struct fv_vertex_hud {
        float x, y;
        float s, t;
};

#endif /* FV_VERTEX_H */
