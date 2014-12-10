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

tiles = {
    '#': 'FV_MAP_TILE_WALL',
    ' ': 'FV_MAP_TILE_FLOOR',
    'T': 'FV_MAP_TILE_TABLE'
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
        if ch not in tiles:
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
const uint8_t
fv_map[FV_MAP_WIDTH * FV_MAP_HEIGHT] = {
''')

for line in reversed(lines):
    for ch in line:
        print("        " + tiles[ch] + ",")

print("};")
