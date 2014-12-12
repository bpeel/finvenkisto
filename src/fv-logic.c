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

#include "config.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "fv-logic.h"
#include "fv-util.h"

/* Movement speed measured in blocks per second */
#define FV_LOGIC_MOVEMENT_SPEED 10.0f

/* Turn speed in radians per second */
#define FV_LOGIC_TURN_SPEED (2.5f * M_PI)

/* Maximum distance to the player from the center point before it will
 * start scrolling */
#define FV_LOGIC_CAMERA_DISTANCE 5.0f

struct fv_logic {
        unsigned int last_ticks;

        float player_x, player_y;
        float current_direction;
        float target_direction;
        bool moving;

        float center_x, center_y;
};

struct fv_logic *
fv_logic_new(void)
{
        struct fv_logic *logic = fv_alloc(sizeof *logic);

        logic->last_ticks = 0;

        logic->player_x = 2.0f;
        logic->player_y = 2.0f;
        logic->current_direction = 0.0f;
        logic->target_direction = 0.0f;
        logic->moving = false;

        logic->center_x = 0.0f;
        logic->center_y = 0.0f;

        return logic;
}

static void
update_center(struct fv_logic *logic)
{
        float dx = logic->player_x - logic->center_x;
        float dy = logic->player_y - logic->center_y;
        float d2, d;

        d2 = dx * dx + dy * dy;

        if (d2 > FV_LOGIC_CAMERA_DISTANCE * FV_LOGIC_CAMERA_DISTANCE) {
                d = sqrtf(d2);
                logic->center_x += dx * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
                logic->center_y += dy * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
        }
}

static void
update_movement(struct fv_logic *logic, float progress_secs)
{
        float distance;
        float diff;
        float turned;

        if (!logic->moving)
                return;

        distance = FV_LOGIC_MOVEMENT_SPEED * progress_secs;

        logic->player_x += distance * cosf(logic->target_direction);
        logic->player_y += distance * sinf(logic->target_direction);

        update_center(logic);

        if (logic->target_direction != logic->current_direction) {
                diff = logic->target_direction - logic->current_direction;

                if (diff > M_PI)
                        diff = diff - 2.0f * M_PI;
                else if (diff < -M_PI)
                        diff = 2.0f * M_PI + diff;

                turned = progress_secs * FV_LOGIC_TURN_SPEED;

                if (turned >= fabsf(diff))
                        logic->current_direction = logic->target_direction;
                else if (diff < 0.0f)
                        logic->current_direction -= turned;
                else
                        logic->current_direction += turned;
        }
}

void
fv_logic_update(struct fv_logic *logic, unsigned int ticks)
{
        unsigned int progress = ticks - logic->last_ticks;
        float progress_secs;

        logic->last_ticks = ticks;

        /* If we've skipped over half a second then we'll assume something
         * has gone wrong and we won't do anything */
        if (progress >= 500 || progress < 0)
                return;

        progress_secs = progress / 1000.0f;

        update_movement(logic, progress_secs);
}

void
fv_logic_set_direction(struct fv_logic *logic,
                       bool moving,
                       float direction)
{
        logic->moving = moving;
        if (moving)
                logic->target_direction = direction;
}

void
fv_logic_free(struct fv_logic *logic)
{
        fv_free(logic);
}

void
fv_logic_get_center(struct fv_logic *logic,
                    float *x, float *y)
{
        *x = logic->center_x;
        *y = logic->center_y;
}

void
fv_logic_for_each_person(struct fv_logic *logic,
                         fv_logic_person_cb person_cb,
                         void *user_data)
{
        struct fv_logic_person person;

        /* Currently the only person is the player */
        person.x = logic->player_x;
        person.y = logic->player_y;
        person.direction = logic->current_direction;

        person_cb(&person, user_data);
}
