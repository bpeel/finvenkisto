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
        { 4.712389, 8.750000, 35.250000,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 4.319690, 10.280734, 34.945518,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 3.926991, 11.578427, 34.078427,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 3.534292, 12.445518, 32.780734,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 3.141592, 12.750000, 31.250000,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 2.748893, 12.445518, 29.719266,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 2.356194, 11.578427, 28.421573,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 1.963495, 10.280734, 27.554482,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 1.570796, 8.750000, 27.250000,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 1.178097, 7.219266, 27.554482,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 0.785398, 5.921573, 28.421573,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 0.392699, 5.054482, 29.719266,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 0.000000, 4.750000, 31.250000,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 5.890487, 5.054482, 32.780734,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 5.497787, 5.921573, 34.078427,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },
        { 5.105088, 7.219266, 34.945518,
          FV_PERSON_TYPE_BAMBISTO,
          FV_PERSON_MOTION_STATIC },

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
