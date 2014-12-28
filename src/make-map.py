# Finvenkisto
#
# Copyright (C) 2014 Neil Roberts
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

MAP_WIDTH = 64
MAP_HEIGHT = 64

MAP_TILE_WIDTH = 8
MAP_TILE_HEIGHT = 8

MAP_TILES_X = MAP_WIDTH // MAP_TILE_WIDTH
MAP_TILES_Y = MAP_HEIGHT // MAP_TILE_HEIGHT

BLOCKS = {
    '#': 'B(FULL_WALL, 0, 15, 15, 15, 15)',
    ' ': 'B(FLOOR, 8, 0, 0, 0, 0)',
    'g': 'B(FLOOR, 10, 0, 0, 0, 0)',
    'w': 'B(FULL_WALL, 0, 12, 12, 15, 15)',
    'd': 'B(FULL_WALL, 0, 15, 12, 12, 15)',
    's': 'B(FULL_WALL, 0, 15, 15, 12, 12)',
    'a': 'B(FULL_WALL, 0, 12, 15, 15, 12)',
    'T': (0, 'B(SPECIAL, 8, 0, 0, 0, 0)'),
    't': 'B(SPECIAL, 8, 0, 0, 0, 0)'
}

line_num = 1

lines = []

for line in sys.stdin:
    line = line.rstrip()
    if len(line) != MAP_WIDTH:
        sys.stderr.write("Line " + str(line_num) + " is " + str(len(line)) +
                         " characters long\n")
        sys.exit(1)

    for ch in line:
        if ch not in BLOCKS:
            sys.stderr.write("Unknown character '" + ch + "' on line " +
                             str(line_num) + "\n")
            sys.exit(1)

    lines.append(line)

    line_num += 1

if line_num != MAP_HEIGHT + 1:
    sys.stderr.write("Map file is " + str(line_num - 1) + " lines long\n")
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

tiles = [[] for x in range(0, MAP_TILES_X * MAP_TILES_Y)]
y = 0

for line in reversed(lines):
    x = 0
    for ch in line:
        block = BLOCKS[ch]
        if isinstance(block, tuple):
            tile = tiles[y // MAP_TILE_HEIGHT * MAP_TILES_X +
                         x // MAP_TILE_WIDTH]
            tile.append((x, y, block[0]))
            block = block[1]
        print("        " + block + ",")
        x += 1
    y += 1


print("};")

print('''
const struct fv_map_tile
fv_map_tiles[FV_MAP_TILES_X * FV_MAP_TILES_Y] = {
''')

for tile in tiles:
    print("        {\n"
          "                {\n")
    for special in tile:
        print("                        {{ {0}, {1}, {2} }},\n".
              format(special[0], special[1], special[2]))
    print("                },\n" +
          "                " + str(len(tile)) + "\n" +
          "        },\n")

print("};")

