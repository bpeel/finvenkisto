# Finvenkisto
#
# Copyright (C) 2015 Neil Roberts
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
import cairo

def init_cr(cr):
    cr.set_font_size(50)
    cr.set_line_width(4)
    cr.set_line_join(cairo.LINE_JOIN_MITER)

def set_path_for_string(cr, s, offset_x, offset_y):
    extents = dummy_cr.text_extents(s)
    (w, h) = extents[2:4]
    (x_bearing, y_bearing) = extents[0:2]

    cr.move_to(-x_bearing + offset_x, -y_bearing + offset_y)
    cr.text_path(s)

dummy_surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, 20, 20)
dummy_cr = cairo.Context(dummy_surface)

init_cr(dummy_cr)

for digit in range(0, 10):
    set_path_for_string(dummy_cr, str(digit), 0, 0)
    extents = dummy_cr.stroke_extents()

    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32,
                                 int(extents[2]) - int(extents[0]),
                                 int(extents[3]) - int(extents[1]))
    cr = cairo.Context(surface)

    init_cr(cr)

    cr.save()
    cr.set_source_rgba(0, 0, 0, 0)
    cr.set_operator(cairo.OPERATOR_SOURCE)
    cr.paint()
    cr.restore()

    set_path_for_string(cr, str(digit), -extents[0], -extents[1])

    cr.set_source_rgba(1, 1, 1, 1)
    cr.stroke_preserve()

    cr.set_source_rgba(68 / 255.0, 103 / 255.0, 235 / 255.0, 1)
    cr.fill()

    surface.write_to_png("hud/digit" + str(digit) + ".png")
