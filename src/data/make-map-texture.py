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
MAX_TEXTURE_SIZE = 1024

def make_vertical_padding(dst, dst_x, dst_y, src, src_y):
    pattern = src.crop((0, src_y, BLOCK_SIZE, src_y + 1))
    for y in range(dst_y, dst_y + PADDING_SIZE):
        dst.paste(pattern, (dst_x, y))

def make_horizontal_padding(dst, dst_x, dst_y, src, src_x, src_y):
    pattern = src.crop((src_x, src_y, src_x + 1, src_y + BLOCK_SIZE))
    for x in range(dst_x, dst_x + PADDING_SIZE):
        dst.paste(pattern, (x, dst_y))

images = list(map(Image.open, sys.argv[2:]))

image_width = 0
image_height = 0
x = 0
y = 0

for image in images:
    if y > 0:
        y += PADDING_SIZE * 2

    if y + image.size[1] > MAX_TEXTURE_SIZE:
        y = 0
        x += BLOCK_SIZE + PADDING_SIZE * 2
        image_width = x + BLOCK_SIZE

    image.position = (x, y)
    y += image.size[1]

    if y > image_height:
        image_height = y

# If the image width is not a power of two then expand it enough so
# that some padding will be added. If the GL driver doesn't support
# NPOT textures then the map painter will then expand it to a power of
# two with empty space at the side. The little bit of padding is
# needed to avoid hitting the empty space when generating the mipmap.
if (image_width & (image_width - 1)):
    image_width += PADDING_SIZE

final_image = Image.new('RGB', (image_width, image_height))

for image in images:
    final_image.paste(image, image.position)

    if image.position[1] > 0:
        make_vertical_padding(final_image,
                              image.position[0],
                              image.position[1] - PADDING_SIZE,
                              image, 0)
        if image.position[0] > 0:
            final_image.paste(image.getpixel((0, 0)),
                              (image.position[0] - PADDING_SIZE,
                               image.position[1] - PADDING_SIZE,
                               image.position[0],
                               image.position[1]))
        if image.position[0] + BLOCK_SIZE < image_width:
            final_image.paste(image.getpixel((BLOCK_SIZE - 1, 0)),
                              (image.position[0] + BLOCK_SIZE,
                               image.position[1] - PADDING_SIZE,
                               image.position[0] + BLOCK_SIZE + PADDING_SIZE,
                               image.position[1]))
    if image.position[1] + image.size[1] < image_height:
        make_vertical_padding(final_image,
                              image.position[0],
                              image.position[1] + image.size[1],
                              image, image.size[1] - 1)
        if image.position[0] > 0:
            final_image.paste(image.getpixel((0, image.size[1] - 1)),
                              (image.position[0] - PADDING_SIZE,
                               image.position[1] + image.size[1],
                               image.position[0],
                               image.position[1] +
                               image.size[1] +
                               PADDING_SIZE))
        if image.position[0] + BLOCK_SIZE < image_width:
            final_image.paste(image.getpixel((BLOCK_SIZE - 1,
                                              image.size[1] - 1)),
                              (image.position[0] + BLOCK_SIZE,
                               image.position[1] + image.size[1],
                               image.position[0] + BLOCK_SIZE + PADDING_SIZE,
                               image.position[1] +
                               image.size[1] +
                               PADDING_SIZE))
    if image.position[0] > 0:
        for part in range(0, image.size[1], BLOCK_SIZE):
            make_horizontal_padding(final_image,
                                    image.position[0] - PADDING_SIZE,
                                    image.position[1] + part,
                                    image, 0, part)
    if image.position[0] + BLOCK_SIZE < image_width:
        for part in range(0, image.size[1], BLOCK_SIZE):
            make_horizontal_padding(final_image,
                                    image.position[0] + BLOCK_SIZE,
                                    image.position[1] + part,
                                    image, BLOCK_SIZE - 1, part)

final_image.save(sys.argv[1])
