# Finvenkisto
#
# Copyright (C) 2014, 2015, 2017 Neil Roberts
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
#     wtxe
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
#  h:          If this is different from the top colour then the tile is either
#              a half-height wall, or an invisible block (ie, a
#              special) depending on whether the walls are marked.
#  x:          The colour is ignored.
#
# The ignored pixels are usually set to something as a reminder if
# there are specials at that location although this script doesn't
# check that.
#
# The area below the main map is used to describe the positions of the
# specials. Each pair of pixels that is not white encodes a special.
# The colours are interpreted as follows:
#
# R1  - x position
# G1  - y position
# B1  - special number
# G2  - Most-significant byte of the rotation
# B2  - Least-significant byte of the rotation
#
# The rotation is a 16-bit number where 65536 represents a full
# rotation.

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

def peek_color(image, bx, by, x, y):
    x += bx * IMAGE_BLOCK_SIZE
    y += by * IMAGE_BLOCK_SIZE

    def make_nibble(v):
        if (v & 0xf) << 4 != (v & 0xf0):
            sys.stderr.write("Bad colour at " + str(x) + ", " + str(y) + "\n")
            sys.exit(1)
        return hex(v >> 4)[2:]

    return ''.join(map(make_nibble, image.getpixel((x, y))[0:3]))

def compare_special(special):
    # Sort by the special number
    return special[3]

def generate_tiles(image):
    tiles = [[] for i in range(0, MAP_TILES_X * MAP_TILES_Y)]

    for x in range(0, image.size[0] - 1, 2):
        for y in range(MAP_HEIGHT * IMAGE_BLOCK_SIZE, image.size[1]):
            pos = image.getpixel((x, y))[0:3]

            if pos == (255, 255, 255):
                continue

            details = image.getpixel((x + 1, y))

            sx = pos[0]
            sy = pos[1]
            special_index = pos[2]
            rotation = (details[1] << 8) | details[2]

            tx = sx // MAP_TILE_WIDTH
            ty = sy // MAP_TILE_HEIGHT
            specials = tiles[tx + ty * MAP_TILES_X]

            specials.append((sx, sy,
                             rotation,
                             special_index))

    for ty in range(0, MAP_TILES_Y):
        for tx in range(0, MAP_TILES_X):
            print("        {\n")

            specials = tiles[tx + ty * MAP_TILES_X]
            # Sort according to the model number so that the render can
            # combine multiple copies of a model into a single draw call
            specials.sort(key = compare_special)

            if len(specials) == 0:
                print("                NULL,")
            else:
                print("                (const struct fv_map_special[])\n"
                      "                {\n")
                for special in specials:
                    print("                        { ",
                          ", ".join(map(str, special)),
                          " },\n")
                print("                },")

            print("                " + str(len(specials)) + "\n" +
                  "        },\n")

image = Image.open(sys.argv[1])

if (image.size[0] < MAP_WIDTH * IMAGE_BLOCK_SIZE or
    image.size[1] < MAP_HEIGHT * IMAGE_BLOCK_SIZE):
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
        half_height_or_special = peek_color(image, x, y, 1, 2) != top_color

        north_color = peek_color(image, x, y, 1, 0)
        if north_color == top_color:
            north = 0
            east = 0
            south = 0
            west = 0

            if half_height_or_special:
                block_type = 'SPECIAL'
            else:
                block_type = 'FLOOR'
        else:
            north = SIDES[north_color]
            east = SIDES[peek_color(image, x, y, 3, 1)]
            south = SIDES[peek_color(image, x, y, 1, 3)]
            west = SIDES[peek_color(image, x, y, 0, 1)]
            if half_height_or_special:
                block_type = 'HALF_WALL'
            else:
                block_type = 'FULL_WALL'
        print('                B(' + block_type, end='')

        for image_index in (top, north, east, south, west):
            print(', ' + str(image_index), end='')

        print('),')

print("        },")

print('''
        .tiles = {
''')

generate_tiles(image)

print('''
        }
};
''')
