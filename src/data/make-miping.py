#!/usr/bin/python3
#
# Finvenkisto
#
# Copyright (C) 2017 Neil Roberts
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

if len(sys.argv) != 3:
    print("usage: {} <infile> <outfile>".format(sys.argv[0]), file=sys.stderr)
    sys.exit(1)

image = Image.open(sys.argv[1])

x = 0
y = 0
(w, h) = image.size
go_right = True

mip_image = Image.new(image.mode, (w * 2, h))

mip_image.paste(image, (0, 0))

while w > 1 or h > 1:
    if go_right:
        x += w
        go_right = False
    else:
        y += h
        go_right = True

    w = max(w // 2, 1)
    h = max(h // 2, 1)

    mip = image.resize((w, h), resample=Image.BICUBIC)

    mip_image.paste(mip, (x, y))

mip_image.save(sys.argv[2], format='png')
