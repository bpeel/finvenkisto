#!/usr/bin/python3

# Finvenkisto
#
# Copyright (C) 2013, 2014 Neil Roberts
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

from PIL import Image
import sys
import os

BLOCK_SIZE = 64
PADDING_SIZE = BLOCK_SIZE // 2

def make_padding(dst, dst_y, src, src_y):
    pattern = src.crop((0, src_y, BLOCK_SIZE, src_y + 1))
    for y in range(dst_y, dst_y + PADDING_SIZE):
        dst.paste(pattern, (0, y))

images = list(map(Image.open, sys.argv[2:]))

total_height = (len(images) - 1) * PADDING_SIZE * 2 + PADDING_SIZE

for image in images:
    total_height += image.size[1]

pot_height = 1
while pot_height < total_height:
    pot_height *= 2

final_image = Image.new('RGB', (BLOCK_SIZE, pot_height))

y = 0
first = True

for image in images:
    if first:
        first = False
    else:
        make_padding(final_image, y, image, 0)
        y += PADDING_SIZE

    final_image.paste(image, (0, y))
    y += image.size[1]

    make_padding(final_image, y, image, image.size[1] - 1)
    y += PADDING_SIZE

final_image.save(sys.argv[1])
