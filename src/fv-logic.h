/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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

#ifndef FV_LOGIC_H
#define FV_LOGIC_H

#include <float.h>
#include <stdbool.h>
#include <math.h>

#include "fv-person.h"

enum fv_logic_state {
        FV_LOGIC_STATE_NO_PLAYERS,
        FV_LOGIC_STATE_RUNNING,
        FV_LOGIC_STATE_FINA_VENKO
};

#define FV_LOGIC_MAX_PLAYERS 4

/* Angle in radians that a shout extends around the player */
#define FV_LOGIC_SHOUT_ANGLE (M_PI / 6.0f)

struct fv_logic_person {
        float direction;
        float x, y;
        enum fv_person_type type;
        bool esperantified;
};

struct fv_logic_shout {
        float x, y;
        float direction;
        float distance;
};

typedef void
(* fv_logic_person_cb)(const struct fv_logic_person *person,
                       void *user_data);

typedef void
(* fv_logic_shout_cb)(const struct fv_logic_shout *person,
                      void *user_data);

struct fv_logic *
fv_logic_new(void);

void
fv_logic_reset(struct fv_logic *logic,
               int n_players);

void
fv_logic_update(struct fv_logic *logic,
                unsigned int ticks);

unsigned int
fv_logic_get_ticks(struct fv_logic *logic);

void
fv_logic_get_center(struct fv_logic *logic,
                    int player_num,
                    float *x, float *y);

void
fv_logic_for_each_person(struct fv_logic *logic,
                         fv_logic_person_cb person_cb,
                         void *user_data);

void
fv_logic_for_each_shout(struct fv_logic *logic,
                        fv_logic_shout_cb shout_cb,
                        void *user_data);

/* The direction is given in radians where 0 is the positive x-axis
 * and the angle is measured counter-clockwise from that. The speed is
 * normalised to the range [0,1].
 */
void
fv_logic_set_direction(struct fv_logic *logic,
                       int player_num,
                       float speed,
                       float direction);

int
fv_logic_get_n_crocodiles(struct fv_logic *logic);

int
fv_logic_get_n_players(struct fv_logic *logic);

enum fv_logic_state
fv_logic_get_state(struct fv_logic *logic);

void
fv_logic_shout(struct fv_logic *logic,
               int player_num);

int
fv_logic_get_score(struct fv_logic *logic,
                   int player_num);

float
fv_logic_get_time_since_fina_venko(struct fv_logic *logic);

void
fv_logic_free(struct fv_logic *logic);

#endif /* FV_LOGIC_H */
