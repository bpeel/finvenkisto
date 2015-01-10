# Finvenkisto
#
# Copyright (C) 2014, 2015 Neil Roberts
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import sys
from PIL import Image

MAP_WIDTH = 40
MAP_HEIGHT = 40

MAP_TILE_WIDTH = 8
MAP_TILE_HEIGHT = 8

MAP_TILES_X = MAP_WIDTH // MAP_TILE_WIDTH
MAP_TILES_Y = MAP_HEIGHT // MAP_TILE_HEIGHT

IMAGE_BLOCK_SIZE = 4

# The map is defined by a PNG image. Each 4x4 rectangle of the image
# represents a block in the map. The colours of the block have the
# following significance:
#
#     xnxx
#     wtpe
#     xhxx
#     xsxx
#
# Where the letters represent the following:
#
#  t:          The image to use for the top of the block. These are indexed
#              from the TOPS dict.
#  n, e, s, w: The colour for the walls of the block. However if the
#              colour is the same as the top of the block then it is ignored.
#              Otherwise they are indexed from the SIDES dict.
#  p:          If this is different from the top of the block then the SPECIALS
#              dict is used to select a special model to place on this block.
#  h:          If this isn't a floor tile and the value is different from t
#              then this is treated as a half-wall block.
#  x:          The colour is ignored.

TOPS = {
    'b95': 8,
    'c90': 0,
    '452': 10,
    'eee': 6
}

SIDES = {
    '644': 12,
    '9cc': 14,
    'ddd': 17,
    'ccc': 20
}

SPECIALS = {
    'd53': 0, # table
    '259': 1, # toilet
    '1df': 2, # teaset
    '000': None # covered by a neighbouring special
}

def peek_color(image, bx, by, x, y):
    x += bx * IMAGE_BLOCK_SIZE
    y += by * IMAGE_BLOCK_SIZE

    def make_nibble(v):
        if (v & 0xf) << 4 != (v & 0xf0):
            sys.stderr.write("Bad colour at " + x + ", " + y + "\n")
            sys.exit(1)
        return hex(v >> 4)[2:]

    return ''.join(map(make_nibble, image.getpixel((x, y))[0:3]))

def generate_tile(image, tx, ty):
    count = 0

    for x in range(tx * MAP_TILE_WIDTH, (tx + 1) * MAP_TILE_WIDTH):
        for y in range((ty + 1) * MAP_TILE_HEIGHT - 1,
                       ty * MAP_TILE_HEIGHT - 1,
                       -1):
            top_color = peek_color(image, x, y, 1, 1)
            special_color = peek_color(image, x, y, 2, 1)

            if special_color != top_color:
                special_index = SPECIALS[special_color]
                if special_index != None:
                    print("                        {{ {0}, {1}, {2} }},\n".
                          format(x, MAP_HEIGHT - 1 - y, special_index))
                    count += 1

    return count

image = Image.open(sys.argv[1])

if image.size != (MAP_WIDTH * IMAGE_BLOCK_SIZE, MAP_HEIGHT * IMAGE_BLOCK_SIZE):
    sys.stderr.write("Map image is not the correct size\n")
    sys.exit(1)

print('''
/* Automatically generated from make-map.py, do not edit */
#include "fv-map.h"
#define F FV_MAP_FULL_WALL
#define H FV_MAP_HALF_WALL
#define B(type, top, n, e, s, w) \\
        (FV_MAP_BLOCK_TYPE_ ## type | \\
         (top) | \\
         ((n) << 6) | \\
         ((e) << 12) | \\
         ((s) << 18) | \\
         ((w) << 24))
const fv_map_block_t
fv_map[FV_MAP_WIDTH * FV_MAP_HEIGHT] = {
''')

for y in range(MAP_HEIGHT - 1, -1, -1):
    for x in range(0, MAP_WIDTH):
        top_color = peek_color(image, x, y, 1, 1)
        top = TOPS[top_color]

        north_color = peek_color(image, x, y, 1, 0)
        if north_color == top_color:
            north = 0
            east = 0
            south = 0
            west = 0

            special_color = peek_color(image, x, y, 2, 1)
            if special_color == top_color:
                block_type = 'FLOOR'
            else:
                block_type = 'SPECIAL'
        else:
            north = SIDES[north_color]
            east = SIDES[peek_color(image, x, y, 3, 1)]
            south = SIDES[peek_color(image, x, y, 1, 3)]
            west = SIDES[peek_color(image, x, y, 0, 1)]
            if peek_color(image, x, y, 1, 2) == top_color:
                block_type = 'FULL_WALL'
            else:
                block_type = 'HALF_WALL'

        print('        B(' + block_type, end='')

        for image_index in (top, north, east, south, west):
            print(', ' + str(image_index), end='')

        print('),')

print("};")

print('''
const struct fv_map_tile
fv_map_tiles[FV_MAP_TILES_X * FV_MAP_TILES_Y] = {
''')

for y in range(MAP_TILES_Y - 1, -1, -1):
    for x in range(0, MAP_TILES_X):
        print("        {\n"
              "                {\n")

        count = generate_tile(image, x, y)

        print("                },\n" +
              "                " + str(count) + "\n" +
              "        },\n")

print("};")
