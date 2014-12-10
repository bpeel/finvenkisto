/*
 * Finvenkisto
 *
 * Copyright (C) 2014 Neil Roberts
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

#ifndef FV_MAP_H
#define FV_MAP_H

#include <stdint.h>

#define FV_MAP_WIDTH 64
#define FV_MAP_HEIGHT 64

#define FV_MAP_FULL_WALL 0x80
#define FV_MAP_HALF_WALL 0x40

#define FV_MAP_IS_FULL_WALL(x) (!!((x) & FV_MAP_FULL_WALL))
#define FV_MAP_IS_HALF_WALL(x) (!!((x) & FV_MAP_HALF_WALL))
#define FV_MAP_IS_WALL(x) (!!((x) & (FV_MAP_FULL_WALL | FV_MAP_HALF_WALL)))

enum fv_map_tile {
        /* Floor pieces */
        FV_MAP_TILE_FLOOR = 0,

        /* Half wall pieces */
        FV_MAP_TILE_TABLE = FV_MAP_HALF_WALL,

        /* Full wall pieces */
        FV_MAP_TILE_WALL = FV_MAP_FULL_WALL
};

extern const uint8_t
fv_map[FV_MAP_WIDTH * FV_MAP_HEIGHT];

#endif /* FV_MAP_H */
