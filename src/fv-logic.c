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
#include "fv-map.h"

/* Player movement speed measured in blocks per second */
#define FV_LOGIC_PLAYER_SPEED 10.0f

/* Turn speed of a person in radians per second */
#define FV_LOGIC_TURN_SPEED (2.5f * M_PI)

/* Maximum distance to the player from the center point before it will
 * start scrolling */
#define FV_LOGIC_CAMERA_DISTANCE 3.0f

/* For collision detection the player is treated as a square of this size */
#define FV_LOGIC_PLAYER_SIZE 0.8f

struct fv_logic_position {
        float x, y;
        float current_direction;
        float target_direction;
        float speed;
};

struct fv_logic_npc {
        struct fv_logic_position position;
};

struct fv_logic {
        unsigned int last_ticks;

        float center_x, center_y;

        struct fv_logic_position player_position;

        struct fv_logic_npc npcs[FV_PERSON_N_NPCS];
};

struct fv_logic *
fv_logic_new(void)
{
        struct fv_logic *logic = fv_alloc(sizeof *logic);
        struct fv_logic_position *position;
        int i;

        logic->last_ticks = 0;

        logic->player_position.x = FV_MAP_WIDTH / 2.0f;
        logic->player_position.y = 0.5f;
        logic->player_position.current_direction = -M_PI / 2.0f;
        logic->player_position.target_direction = 0.0f;
        logic->player_position.speed = 0.0f;

        logic->center_x = logic->player_position.x;
        logic->center_y = logic->player_position.y;

        for (i = 0; i < FV_PERSON_N_NPCS; i++) {
                position = &logic->npcs[i].position;
                position->x = fv_person_npcs[i].x;
                position->y = fv_person_npcs[i].y;
                position->current_direction = fv_person_npcs[i].direction;
                position->target_direction = 0.0f;
                position->speed = 0.0f;
        }

        return logic;
}

static bool
is_wall(int x, int y)
{
        if (x < 0 || x >= FV_MAP_WIDTH ||
            y < 0 || y >= FV_MAP_HEIGHT)
                return true;

        return FV_MAP_IS_WALL(fv_map[y * FV_MAP_WIDTH + x]);
}

static void
update_position_direction(struct fv_logic *logic,
                          struct fv_logic_position *position,
                          float progress_secs)
{
        float diff, turned;

        if (position->speed == 0.0f)
                return;

        if (position->target_direction == position->current_direction)
                return;

        diff = position->target_direction - position->current_direction;

        if (diff > M_PI)
                diff = diff - 2.0f * M_PI;
        else if (diff < -M_PI)
                diff = 2.0f * M_PI + diff;

        turned = progress_secs * FV_LOGIC_TURN_SPEED;

        if (turned >= fabsf(diff))
                position->current_direction =
                        position->target_direction;
        else if (diff < 0.0f)
                position->current_direction -= turned;
        else
                position->current_direction += turned;
}

static void
update_position_xy(struct fv_logic *logic,
                   struct fv_logic_position *position,
                   float progress_secs)
{
        float distance;
        float diff;
        float pos;

        if (position->speed == 0.0f)
                return;

        distance = position->speed * progress_secs;

        diff = distance * cosf(position->target_direction);

        /* Don't let the player move more than one tile per frame
         * because otherwise it might be possible to skip over
         * walls */
        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (position->x + diff +
               copysignf(FV_LOGIC_PLAYER_SIZE / 2.0f, diff));
        if (!is_wall(floorf(pos),
                     floorf(position->y + FV_LOGIC_PLAYER_SIZE / 2.0f)) &&
            !is_wall(floorf(pos),
                     floorf(position->y - FV_LOGIC_PLAYER_SIZE / 2.0f)))
                position->x += diff;

        diff = distance * sinf(position->target_direction);

        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (position->y + diff +
               copysignf(FV_LOGIC_PLAYER_SIZE / 2.0f, diff));
        if (!is_wall(floorf(position->x + FV_LOGIC_PLAYER_SIZE / 2.0f),
                     floorf(pos)) &&
            !is_wall(floorf(position->x - FV_LOGIC_PLAYER_SIZE / 2.0f),
                     floorf(pos)))
                position->y += diff;
}

static void
update_position(struct fv_logic *logic,
                struct fv_logic_position *position,
                float progress_secs)
{
        update_position_xy(logic, position, progress_secs);
        update_position_direction(logic, position, progress_secs);
}

static void
update_center(struct fv_logic *logic)
{
        float dx = logic->player_position.x - logic->center_x;
        float dy = logic->player_position.y - logic->center_y;
        float d2, d;

        d2 = dx * dx + dy * dy;

        if (d2 > FV_LOGIC_CAMERA_DISTANCE * FV_LOGIC_CAMERA_DISTANCE) {
                d = sqrtf(d2);
                logic->center_x += dx * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
                logic->center_y += dy * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
        }
}

static void
update_player_movement(struct fv_logic *logic, float progress_secs)
{
        if (!logic->player_position.speed)
                return;

        update_position(logic, &logic->player_position, progress_secs);
        update_center(logic);
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

        update_player_movement(logic, progress_secs);
}

void
fv_logic_set_direction(struct fv_logic *logic,
                       bool moving,
                       float direction)
{
        if (moving) {
                logic->player_position.speed = FV_LOGIC_PLAYER_SPEED;
                logic->player_position.target_direction = direction;
        } else {
                logic->player_position.speed = 0.0f;
        }
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
        int i;

        /* Currently the only person is the player */
        person.x = logic->player_position.x;
        person.y = logic->player_position.y;
        person.direction = logic->player_position.current_direction;
        person.type = FV_PERSON_TYPE_FINVENKISTO;

        person_cb(&person, user_data);

        for (i = 0; i < FV_PERSON_N_NPCS; i++) {
                person.x = logic->npcs[i].position.x;
                person.y = logic->npcs[i].position.y;
                person.direction = logic->npcs[i].position.current_direction;
                person.type = fv_person_npcs[i].type;

                person_cb(&person, user_data);
        }
}
