/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015 Neil Roberts
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

/* This is the vector [-1, 2, -3] rotated by -30° along the x axis and
 * then normalised. This was calculated with this snippet of python:
 * import numpy
 * import math
 * angle = -30.0 * math.pi / 180.0
 * rotation = [[1.0, 0.0, 0.0],
 *             [0.0, math.cos(angle), -math.sin(angle)],
 *             [0.0, math.sin(angle), math.cos(angle)]]
 * norm = [-1, 2, -3]
 * trans_norm = numpy.dot(rotation, norm)
 * norm_norm = numpy.divide(trans_norm,
 *                          numpy.sqrt(numpy.dot(trans_norm, trans_norm)))
 * print norm_norm
 */
const vec3 light_direction = vec3(-0.26726124, 0.06201819, -0.96162632);

const float ambient_light = 0.5;
const float diffuse = 1.0 - ambient_light;

float
get_lighting_tint(mat3 normal_transform,
                  vec3 normal)
{
        /* Transform the normal */
        normal = normal_transform * normal;
        /* Start with the ambient light */
        float light_factor = ambient_light;
        /* Calculate the diffuse factor based on the angle between the
           vertex normal and light direction */
        float diffuse_factor = max(0.0, dot(-light_direction, normal));
        /* Skip the diffuse term if the vertex is not facing
           the light */
        if (diffuse_factor > 0.0) {
                /* Add the diffuse term */
                light_factor += diffuse_factor * diffuse;
        }

        return light_factor;
}
