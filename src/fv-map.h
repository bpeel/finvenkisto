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

#define FV_MAP_WIDTH 40
#define FV_MAP_HEIGHT 40

#define FV_MAP_TILE_WIDTH 8
#define FV_MAP_TILE_HEIGHT 8
#define FV_MAP_MAX_SPECIALS 8

#define FV_MAP_TILES_X (FV_MAP_WIDTH / FV_MAP_TILE_WIDTH)
#define FV_MAP_TILES_Y (FV_MAP_HEIGHT / FV_MAP_TILE_HEIGHT)

_Static_assert(FV_MAP_WIDTH % FV_MAP_TILE_WIDTH == 0,
               "The map size must be a multiple of the tile size");
_Static_assert(FV_MAP_HEIGHT % FV_MAP_TILE_HEIGHT == 0,
               "The map size must be a multiple of the tile size");

#define FV_MAP_BLOCK_TYPE_SHIFT 30
#define FV_MAP_BLOCK_TYPE_MASK ((uint32_t) 0x3 << FV_MAP_BLOCK_TYPE_SHIFT)

enum fv_map_block_type {
        FV_MAP_BLOCK_TYPE_FLOOR = 0 << FV_MAP_BLOCK_TYPE_SHIFT,
        FV_MAP_BLOCK_TYPE_HALF_WALL = 1 << FV_MAP_BLOCK_TYPE_SHIFT,
        FV_MAP_BLOCK_TYPE_FULL_WALL = 2 << FV_MAP_BLOCK_TYPE_SHIFT,
        FV_MAP_BLOCK_TYPE_SPECIAL = 3 << FV_MAP_BLOCK_TYPE_SHIFT,
};

#define FV_MAP_GET_BLOCK_TYPE(x) ((x) & FV_MAP_BLOCK_TYPE_MASK)
#define FV_MAP_GET_BLOCK_TOP_IMAGE(x) ((x) & ((1 << 6) - 1))
#define FV_MAP_GET_BLOCK_NORTH_IMAGE(x) (((x) >> 6) & ((1 << 6) - 1))
#define FV_MAP_GET_BLOCK_EAST_IMAGE(x) (((x) >> 12) & ((1 << 6) - 1))
#define FV_MAP_GET_BLOCK_SOUTH_IMAGE(x) (((x) >> 18) & ((1 << 6) - 1))
#define FV_MAP_GET_BLOCK_WEST_IMAGE(x) (((x) >> 24) & ((1 << 6) - 1))
#define FV_MAP_IS_WALL(x) (FV_MAP_GET_BLOCK_TYPE(x) != FV_MAP_BLOCK_TYPE_FLOOR)

typedef uint32_t fv_map_block_t;

extern const fv_map_block_t
fv_map[FV_MAP_WIDTH * FV_MAP_HEIGHT];

struct fv_map_special {
        uint16_t x, y;
        uint16_t rotation;
        uint16_t num;
};

struct fv_map_tile {
        struct fv_map_special specials[FV_MAP_MAX_SPECIALS];
        int n_specials;
};

extern const struct fv_map_tile
fv_map_tiles[FV_MAP_TILES_X * FV_MAP_TILES_Y];

#endif /* FV_MAP_H */
