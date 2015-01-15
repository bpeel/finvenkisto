/*
 * Finvenkisto
 *
 * Copyright (C) 2015 Neil Roberts
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

#include "config.h"

#include <math.h>

#include "fv-person.h"

const struct fv_person_npc
fv_person_npcs[FV_PERSON_N_NPCS] = {
        /* People outside the center doing la bamba */
        { 4.712389, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 4.319690, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 3.926991, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 3.534292, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 3.141592, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 2.748893, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 2.356194, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 1.963495, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 1.570796, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 1.178097, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 0.785398, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 0.392699, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 0.000000, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 5.890487, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 5.497787, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },
        { 5.105088, 8.75, 31.25,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_CIRCLE,
          .circle = { .radius = 4.0 } },

        /* People milling around in the center of La Bamba */
        { 5.301438, 10.681195, 32.094707,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 3.632467, 8.175107, 32.485060,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 6.062292, 7.429351, 29.578986,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 1.570796, 6.010022, 31.474161,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 5.546875, 9.493217, 29.045753,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 5.031457, 9.684329, 30.633960,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 5.417243, 9.124920, 33.492310,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 1.827340, 6.418345, 32.791150,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
};
