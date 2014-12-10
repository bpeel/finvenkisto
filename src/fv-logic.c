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

struct fv_logic {
        unsigned int last_ticks;

        float direction;
        bool moving;

        float center_x, center_y;
};

struct fv_logic *
fv_logic_new(void)
{
        struct fv_logic *logic = fv_alloc(sizeof *logic);

        logic->last_ticks = 0;
        logic->moving = false;
        logic->direction = 0.0f;
        logic->center_x = 0.0f;
        logic->center_y = 0.0f;

        return logic;
}

static void
update_movement(struct fv_logic *logic, float progress_secs)
{
        float distance;

        if (!logic->moving)
                return;

        distance = FV_LOGIC_MOVEMENT_SPEED * progress_secs;

        logic->center_x += distance * cosf(logic->direction);
        logic->center_y += distance * sinf(logic->direction);
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
        logic->direction = direction;
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
