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

#ifndef FV_PERSON_H
#define FV_PERSON_H

#include <stdint.h>

#define FV_PERSON_N_NPCS 26

enum fv_person_type {
        FV_PERSON_TYPE_FINVENKISTO,
        FV_PERSON_TYPE_BAMBISTO1,
        FV_PERSON_TYPE_BAMBISTO2,
        FV_PERSON_TYPE_BAMBISTO3,
        FV_PERSON_TYPE_GUFUJESTRO,
        FV_PERSON_TYPE_TOILET_GUY,
};

enum fv_person_motion {
        FV_PERSON_MOTION_STATIC,
        FV_PERSON_MOTION_CIRCLE,
        FV_PERSON_MOTION_RANDOM
};

struct fv_person_npc {
        float direction;
        float x, y;
        enum fv_person_type type;
        enum fv_person_motion motion;

        /* Data depending on the type of motion */
        union {
                struct {
                        float radius;
                } circle;

                struct {
                        /* The NPCs will walk to random locations
                         * within this circle */
                        float center_x, center_y;
                        float radius;
                        /* Time in milliseconds between picking a new
                         * target */
                        uint32_t retarget_time;
                } random;
        };
};

extern const struct fv_person_npc
fv_person_npcs[FV_PERSON_N_NPCS];

#endif /* FV_PERSON_H */
