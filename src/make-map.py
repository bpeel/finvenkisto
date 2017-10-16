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
MAP_HEIGHT = 48

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
#     xhrx
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
#  r:          Rotation about the z-axis for the special model. Ignored if the
#              color is the same as the top of the block, otherwise
#              the r and g components together make a 16-bit rotation
#              value where 65536 is a full circle.
#  h:          If this isn't a floor tile and the value is different from t
#              then this is treated as a half-wall block.
#  x:          The colour is ignored.

TOPS = {
    'b95': 4, # brick flooring
    'c90': 0, # wall top
    '452': 6, # grass
    'eee': 2, # bathroom floor
    '522': 19, # room floor
    '933': 21, # wood
    '54c': 31, # sleeping bag 1
    '54d': 32 # sleeping bag 2
}

SIDES = {
    '644': 8, # brick wall
    '9cc': 11, # inner wall
    'ddd': 14, # bathroom wall
    'ccc': 16, # bathroom wall special
    '911': 23, # table side
    '56c': 25, # welcome poster 1
    '56d': 28, # welcome poster 2
    '001': 34, # chalkboard 1
    '002': 37, # chalkboard 2
}

SPECIALS = {
    'd53': 0, # table
    '259': 1, # toilet
    '1df': 2, # teaset
    '00e': 3, # chair
    'd55': 4, # bed
    'b3b': 5, # barrel
    '000': None # covered by a neighbouring special
}

def peek_color(image, bx, by, x, y):
    x += bx * IMAGE_BLOCK_SIZE
    y += by * IMAGE_BLOCK_SIZE

    def make_nibble(v):
        if (v & 0xf) << 4 != (v & 0xf0):
            sys.stderr.write("Bad colour at " + str(x) + ", " + str(y) + "\n")
            sys.exit(1)
        return hex(v >> 4)[2:]

    return ''.join(map(make_nibble, image.getpixel((x, y))[0:3]))

def get_rotation(image, x, y):
    top_color = image.getpixel((x * IMAGE_BLOCK_SIZE + 1,
                                y * IMAGE_BLOCK_SIZE + 1))
    rotation_color = image.getpixel((x * IMAGE_BLOCK_SIZE + 2,
                                     y * IMAGE_BLOCK_SIZE + 2))
    if top_color == rotation_color:
        return 0
    else:
        return (rotation_color[0] << 8) | rotation_color[1]

def generate_tile(image, tx, ty):
    specials = []

    for x in range(tx * MAP_TILE_WIDTH, (tx + 1) * MAP_TILE_WIDTH):
        for y in range((ty + 1) * MAP_TILE_HEIGHT - 1,
                       ty * MAP_TILE_HEIGHT - 1,
                       -1):
            top_color = peek_color(image, x, y, 1, 1)
            special_color = peek_color(image, x, y, 2, 1)

            if special_color != top_color:
                special_index = SPECIALS[special_color]
                if special_index != None:
                    rotation = get_rotation(image, x, y)
                    specials.append((x, MAP_HEIGHT - 1 - y,
                                     rotation,
                                     special_index))

    # Sort according to the model number so that the render can
    # combine multiple copies of a model into a single draw call
    specials.sort(key = lambda x: x[3])

    for special in specials:
        print("                        { ",
              ", ".join(map(str, special)),
              " },\n")

    return len(specials)

image = Image.open(sys.argv[1])

if image.size != (MAP_WIDTH * IMAGE_BLOCK_SIZE, MAP_HEIGHT * IMAGE_BLOCK_SIZE):
    sys.stderr.write("Map image is not the correct size\n")
    sys.exit(1)

print('''
/* Automatically generated from make-map.py, do not edit */
#include "config.h"
#include <stdlib.h>
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
const struct fv_map
fv_map = {
        .blocks = {
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

        print('                B(' + block_type, end='')

        for image_index in (top, north, east, south, west):
            print(', ' + str(image_index), end='')

        print('),')

print("        },")

print('''
        .tiles = {
''')

for y in range(MAP_TILES_Y - 1, -1, -1):
    for x in range(0, MAP_TILES_X):
        print("                {\n"
              "                        {\n")

        count = generate_tile(image, x, y)

        print("                        },\n" +
              "                        " + str(count) + "\n" +
              "                },\n")

print('''
        }
};
''')
