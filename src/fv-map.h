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

#define FV_MAP_FULL_WALL 0x8000
#define FV_MAP_HALF_WALL 0x4000

#define FV_MAP_IS_FULL_WALL(x) (!!((x) & FV_MAP_FULL_WALL))
#define FV_MAP_IS_HALF_WALL(x) (!!((x) & FV_MAP_HALF_WALL))
#define FV_MAP_IS_WALL(x) (!!((x) & (FV_MAP_FULL_WALL | FV_MAP_HALF_WALL)))
#define FV_MAP_GET_BLOCK_TOP_IMAGE(x) ((x) & ((1 << 7) - 1))
#define FV_MAP_GET_BLOCK_SIDE_IMAGE(x) (((x) >> 7) & ((1 << 7) - 1))

typedef uint16_t fv_map_block_t;

extern const fv_map_block_t
fv_map[FV_MAP_WIDTH * FV_MAP_HEIGHT];

#endif /* FV_MAP_H */
