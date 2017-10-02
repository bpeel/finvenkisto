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

/* Movement speed of an afraid NPC */
#define FV_LOGIC_NPC_RUN_SPEED (FV_LOGIC_PLAYER_SPEED * 0.7f)

/* Movement speed of a returning NPC */
#define FV_LOGIC_NPC_WALK_SPEED (FV_LOGIC_NPC_RUN_SPEED * 0.5f)

/* Turn speed of a person in radians per second */
#define FV_LOGIC_TURN_SPEED (2.5f * M_PI)

/* Maximum distance to the player from the center point before it will
 * start scrolling */
#define FV_LOGIC_CAMERA_DISTANCE 3.0f

/* The size of a person. When checking against something this
 * represents a square centered at the person's position. When being
 * checked against for person-person collisions it is a circle with
 * this diameter */
#define FV_LOGIC_PERSON_SIZE 0.8f

/* If the player is closer than this distance to an NPC then they will
 * become afraid */
#define FV_LOGIC_FEAR_DISTANCE 2.0f

/* and they will stop being afraid at this distance */
#define FV_LOGIC_SAFE_DISTANCE 6.0f

/* If a returning person is closer than this distance to their target
 * position then they'll just jump to it instead. This is the distance
 * the walking person can travel in a 60th of a second */
#define FV_LOGIC_LOCK_DISTANCE (FV_LOGIC_NPC_WALK_SPEED / 60.0f)

/* Speed of rotation for the NPCs who move in a circle in radians per
 * second */
#define FV_LOGIC_CIRCLE_SPEED 0.2f

/* Gap between player positions at the start of the game */
#define FV_LOGIC_PLAYER_START_GAP 2.0f

/* Length of a fully extended shout */
#define FV_LOGIC_SHOUT_LENGTH 4.0f

/* Time taken for the shout distance to reach its full extent in
 * seconds */
#define FV_LOGIC_SHOUT_GROWTH_TIME 0.5f

/* Time that a shout stays around for once it is fully extended */
#define FV_LOGIC_SHOUT_LINGER_TIME 0.2f

struct fv_logic_position {
        float x, y;
        float current_direction;
        float target_direction;
        float speed;
};

enum fv_logic_npc_state {
        FV_LOGIC_NPC_STATE_NORMAL,
        FV_LOGIC_NPC_STATE_AFRAID,
        FV_LOGIC_NPC_STATE_RETURNING
};

struct fv_logic_npc {
        struct fv_logic_position position;
        enum fv_logic_npc_state state;
        bool esperantified;

        /* State depending on the motion type */
        union {
                struct {
                        float target_x, target_y;
                        unsigned int last_target_time;
                } random;
        };
};

struct fv_logic_player {
        struct fv_logic_position position;
        float center_x, center_y;
        int score;

        /* The other two shout fields are invalid if this is false */
        bool shouting;
        /* The current extent of the shout. Updated in
         * fv_logic_update */
        float shout_distance;
        /* Time in seconds since the player started shouting */
        float shout_time;
};

struct fv_logic {
        enum fv_logic_state state;

        unsigned int last_ticks;

        struct fv_logic_player players[FV_LOGIC_MAX_PLAYERS];
        int n_players;

        struct fv_logic_npc npcs[FV_PERSON_N_NPCS];

        /* Updated at the beginning of fv_logic_update and is set to
         * true if any of the players are shouting */
        bool anyone_shouting;

        /* Number of NPCs that have been shouted at. Once this reaches
         * FV_PERSON_N_NPCS the fina venko comes and the game ends */
        int n_esperantified;

        /* Tick time that the state was changed to
         * FV_LOGIC_STATE_FINA_VENKO */
        unsigned int fina_venko_time;
};

static void
init_npc(struct fv_logic *logic,
         int npc_num)
{
        struct fv_logic_npc *npc = logic->npcs + npc_num;
        const struct fv_person_npc *initial_state = fv_person_npcs + npc_num;
        struct fv_logic_position *position = &npc->position;

        npc->state = FV_LOGIC_NPC_STATE_NORMAL;
        npc->esperantified = false;
        position->target_direction = 0.0f;
        position->speed = 0.0f;

        switch (initial_state->motion) {
        case FV_PERSON_MOTION_STATIC:
                position->x = initial_state->x;
                position->y = initial_state->y;
                position->current_direction = initial_state->direction;
                break;

        case FV_PERSON_MOTION_CIRCLE:
                position->x = (initial_state->x -
                               initial_state->circle.radius *
                               cosf(initial_state->direction));
                position->y = (initial_state->y -
                               initial_state->circle.radius *
                               sinf(initial_state->direction));
                position->current_direction = initial_state->direction;
                break;

        case FV_PERSON_MOTION_RANDOM:
                position->x = initial_state->x;
                position->y = initial_state->y;
                position->current_direction = initial_state->direction;
                npc->random.target_x = position->x;
                npc->random.target_y = position->y;
                npc->random.last_target_time = 0;
                break;
        }
}

void
fv_logic_reset(struct fv_logic *logic,
               int n_players)
{
        struct fv_logic_player *player;
        int i;

        logic->last_ticks = 0;
        logic->n_players = n_players;
        logic->n_esperantified = 0;
        logic->anyone_shouting = false;

        for (i = 0; i < n_players; i++) {
                player = logic->players + i;
                player->position.x = (FV_MAP_START_X -
                                      (n_players - 1) *
                                      FV_LOGIC_PLAYER_START_GAP / 2.0f +
                                      i * FV_LOGIC_PLAYER_START_GAP);
                player->position.y = FV_MAP_START_Y;
                player->position.current_direction = -M_PI / 2.0f;
                player->position.target_direction = 0.0f;
                player->position.speed = 0.0f;

                player->shouting = false;

                player->center_x = player->position.x;
                player->center_y = player->position.y;

                player->score = 0;
        }

        for (i = 0; i < FV_PERSON_N_NPCS; i++)
                init_npc(logic, i);

        if (n_players == 0)
                logic->state = FV_LOGIC_STATE_NO_PLAYERS;
        else
                logic->state = FV_LOGIC_STATE_RUNNING;
}

struct fv_logic *
fv_logic_new(void)
{
        struct fv_logic *logic = fv_alloc(sizeof *logic);

        fv_logic_reset(logic, 0);

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

static bool
position_in_range(const struct fv_logic_position *position,
                  float x, float y,
                  float distance)
{
        float dx = x - position->x;
        float dy = y - position->y;

        return dx * dx + dy * dy < distance * distance;
}

static bool
person_blocking(const struct fv_logic *logic,
                const struct fv_logic_position *this_position,
                float x, float y)
{
        int i;

        for (i = 0; i < logic->n_players; i++) {
                if (this_position == &logic->players[i].position)
                        continue;

                if (position_in_range(&logic->players[i].position,
                                      x, y,
                                      FV_LOGIC_PERSON_SIZE / 2.0f))
                        return true;
        }

        for (i = 0; i < FV_PERSON_N_NPCS; i++) {
                if (this_position == &logic->npcs[i].position)
                        continue;

                if (position_in_range(&logic->npcs[i].position,
                                      x, y,
                                      FV_LOGIC_PERSON_SIZE / 2.0f))
                        return true;
        }

        return false;
}

static void
update_position_direction(struct fv_logic *logic,
                          struct fv_logic_position *position,
                          float progress_secs)
{
        float diff, turned;

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

        distance = position->speed * progress_secs;

        diff = distance * cosf(position->target_direction);

        /* Don't let the player move more than one tile per frame
         * because otherwise it might be possible to skip over
         * walls */
        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (position->x + diff +
               copysignf(FV_LOGIC_PERSON_SIZE / 2.0f, diff));
        if (!is_wall(floorf(pos),
                     floorf(position->y + FV_LOGIC_PERSON_SIZE / 2.0f)) &&
            !is_wall(floorf(pos),
                     floorf(position->y - FV_LOGIC_PERSON_SIZE / 2.0f)) &&
            !person_blocking(logic, position, pos, position->y))
                position->x += diff;

        diff = distance * sinf(position->target_direction);

        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (position->y + diff +
               copysignf(FV_LOGIC_PERSON_SIZE / 2.0f, diff));
        if (!is_wall(floorf(position->x + FV_LOGIC_PERSON_SIZE / 2.0f),
                     floorf(pos)) &&
            !is_wall(floorf(position->x - FV_LOGIC_PERSON_SIZE / 2.0f),
                     floorf(pos)) &&
            !person_blocking(logic, position, position->x, pos))
                position->y += diff;
}

static void
update_position(struct fv_logic *logic,
                struct fv_logic_position *position,
                float progress_secs)
{
        if (position->speed == 0.0f)
                return;

        update_position_xy(logic, position, progress_secs);
        update_position_direction(logic, position, progress_secs);
}

static void
update_center(struct fv_logic_player *player)
{
        float dx = player->position.x - player->center_x;
        float dy = player->position.y - player->center_y;
        float d2, d;

        d2 = dx * dx + dy * dy;

        if (d2 > FV_LOGIC_CAMERA_DISTANCE * FV_LOGIC_CAMERA_DISTANCE) {
                d = sqrtf(d2);
                player->center_x += dx * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
                player->center_y += dy * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
        }
}

static void
update_player_movement(struct fv_logic *logic,
                       struct fv_logic_player *player,
                       float progress_secs)
{
        if (!player->position.speed)
                return;

        update_position(logic, &player->position, progress_secs);
        update_center(player);
}

static void
update_npc_static_movement(struct fv_logic *logic,
                           int npc_num,
                           float progress_secs)
{
        struct fv_logic_npc *npc = logic->npcs + npc_num;
        const struct fv_person_npc *initial_state = fv_person_npcs + npc_num;

        if (npc->state == FV_LOGIC_NPC_STATE_RETURNING &&
            position_in_range(&npc->position,
                              initial_state->x,
                              initial_state->y,
                              FV_LOGIC_LOCK_DISTANCE)) {
                npc->position.x = initial_state->x;
                npc->position.y = initial_state->y;
                npc->position.speed = 0.0f;
                npc->state = FV_LOGIC_NPC_STATE_NORMAL;
        }

        if (npc->state == FV_LOGIC_NPC_STATE_NORMAL) {
                npc->position.target_direction = initial_state->direction;
                update_position_direction(logic, &npc->position, progress_secs);
        } else {
                npc->position.target_direction =
                        atan2(initial_state->y - npc->position.y,
                              initial_state->x - npc->position.x);

                if (npc->position.target_direction < 0)
                        npc->position.target_direction += M_PI * 2.0f;

                npc->position.speed = FV_LOGIC_NPC_WALK_SPEED;

                update_position(logic, &npc->position, progress_secs);
        }
}

static void
update_npc_circle_movement(struct fv_logic *logic,
                           int npc_num,
                           float progress_secs)
{
        struct fv_logic_npc *npc = logic->npcs + npc_num;
        const struct fv_person_npc *initial_state = fv_person_npcs + npc_num;
        float target_x, target_y;
        float facing_angle;

        facing_angle = (logic->last_ticks * FV_LOGIC_CIRCLE_SPEED /
                        1000.0f + initial_state->direction);
        target_x = (initial_state->x -
                    initial_state->circle.radius * cosf(facing_angle));
        target_y = (initial_state->y -
                    initial_state->circle.radius * sinf(facing_angle));

        if (npc->state == FV_LOGIC_NPC_STATE_RETURNING) {
                /* Check if the person is within a block of where they
                 * should be be (ie, not where they are headed) */
                if (position_in_range(&npc->position,
                                      target_x,
                                      target_y,
                                      1.0f))
                        npc->state = FV_LOGIC_NPC_STATE_NORMAL;
        }

        if (npc->state == FV_LOGIC_NPC_STATE_NORMAL) {
                /* Work out the speed along the diameter based on the
                 * turning circle speed.
                 *
                 * r = radius of circle
                 * v = turn speed in radians per second
                 * circumference = 2πr
                 * distance along circumference of one radian = 2πr / 2π
                 * speed along circumference = 2πrv / 2π
                 *                           = rv
                 */
                npc->position.speed =
                        initial_state->circle.radius * FV_LOGIC_CIRCLE_SPEED;
        } else {
                npc->position.speed =
                        FV_LOGIC_NPC_WALK_SPEED;
        }

        npc->position.target_direction =
                atan2(target_y - npc->position.y,
                      target_x - npc->position.x);

        update_position_xy(logic, &npc->position, progress_secs);

        if (npc->state == FV_LOGIC_NPC_STATE_NORMAL)
                npc->position.target_direction =
                        fmodf(facing_angle, 2.0f * M_PI);

        update_position_direction(logic, &npc->position, progress_secs);
}

static void
update_npc_random_movement(struct fv_logic *logic,
                           int npc_num,
                           float progress_secs)
{
        struct fv_logic_npc *npc = logic->npcs + npc_num;
        const struct fv_person_npc *initial_state = fv_person_npcs + npc_num;
        float target_angle, target_radius;

        if (logic->last_ticks - npc->random.last_target_time >=
            initial_state->random.retarget_time) {
                npc->position.speed = FV_LOGIC_NPC_WALK_SPEED;
                npc->state = FV_LOGIC_NPC_STATE_RETURNING;

                target_angle = rand() * 2.0f * M_PI / RAND_MAX;
                target_radius = (rand() * initial_state->random.radius /
                                 RAND_MAX);
                npc->random.target_x = (sinf(target_angle) * target_radius +
                                        initial_state->random.center_x);
                npc->random.target_y = (cosf(target_angle) * target_radius +
                                        initial_state->random.center_y);

                npc->random.last_target_time = logic->last_ticks;
        }

        if (npc->state == FV_LOGIC_NPC_STATE_RETURNING) {
                if (position_in_range(&npc->position,
                                      npc->random.target_x,
                                      npc->random.target_y,
                                      FV_LOGIC_LOCK_DISTANCE)) {
                        npc->position.speed = 0.0f;
                        npc->state = FV_LOGIC_NPC_STATE_NORMAL;
                } else {
                        npc->position.target_direction =
                                atan2(npc->random.target_y - npc->position.y,
                                      npc->random.target_x - npc->position.x);

                        if (npc->position.target_direction < 0)
                                npc->position.target_direction += M_PI * 2.0f;

                        update_position(logic, &npc->position, progress_secs);
                }
        }
}

static void
update_npc_normal_movement(struct fv_logic *logic,
                           int npc_num,
                           float progress_secs)
{
        const struct fv_person_npc *initial_state = fv_person_npcs + npc_num;

        /* NPCs who have been esperantified are fed up and can't be
         * bothered to move apart from to run away */
        if (logic->npcs[npc_num].esperantified)
                return;

        switch (initial_state->motion) {
        case FV_PERSON_MOTION_STATIC:
                update_npc_static_movement(logic, npc_num, progress_secs);
                break;

        case FV_PERSON_MOTION_CIRCLE:
                update_npc_circle_movement(logic, npc_num, progress_secs);
                break;

        case FV_PERSON_MOTION_RANDOM:
                update_npc_random_movement(logic, npc_num, progress_secs);
                break;
        }
}

static void
update_npc_movement(struct fv_logic *logic,
                    int npc_num,
                    float progress_secs)
{
        struct fv_logic_npc *npc = logic->npcs + npc_num;
        struct fv_logic_player *player, *nearest_player = NULL;
        float nearest_distance2 = FLT_MAX;
        float dx, dy, distance2;
        int i;

        for (i = 0; i < logic->n_players; i++) {
                player = logic->players + i;
                dx = player->position.x - npc->position.x;
                dy = player->position.y - npc->position.y;
                distance2 = dx * dx + dy * dy;

                if (distance2 < nearest_distance2) {
                        nearest_distance2 = distance2;
                        nearest_player = player;
                }
        }

        if (npc->state == FV_LOGIC_NPC_STATE_AFRAID) {
                /* Stop being afraid once the player is far enough
                 * away */
                if (nearest_distance2 >= (FV_LOGIC_SAFE_DISTANCE *
                                          FV_LOGIC_SAFE_DISTANCE))
                        npc->state = FV_LOGIC_NPC_STATE_RETURNING;
        } else if (nearest_distance2 < (FV_LOGIC_FEAR_DISTANCE *
                                        FV_LOGIC_FEAR_DISTANCE)) {
                npc->state = FV_LOGIC_NPC_STATE_AFRAID;
        }

        if (npc->state == FV_LOGIC_NPC_STATE_AFRAID) {
                /* Run directly away from the nearest player */
                npc->position.target_direction =
                        atan2(npc->position.y - nearest_player->position.y,
                              npc->position.x - nearest_player->position.x);
                if (npc->position.target_direction < 0)
                        npc->position.target_direction += M_PI * 2.0f;
                npc->position.speed = FV_LOGIC_NPC_RUN_SPEED;

                update_position(logic, &npc->position, progress_secs);
        } else {
                update_npc_normal_movement(logic, npc_num, progress_secs);
        }
}

static bool
shout_in_range(struct fv_logic_player *player,
               struct fv_logic_npc *npc)
{
        float npc_angle, diff;

        if (!position_in_range(&player->position,
                               npc->position.x, npc->position.y,
                               player->shout_distance +
                               FV_LOGIC_PERSON_SIZE / 2.0f))
                return false;

        npc_angle = atan2(npc->position.y - player->position.y,
                          npc->position.x - player->position.x);
        diff = fabsf(npc_angle - player->position.current_direction);

        if (diff > M_PI)
                diff = 2.0f * M_PI - diff;

        return diff <= FV_LOGIC_SHOUT_ANGLE;
}

static void
esperantify(struct fv_logic *logic,
            struct fv_logic_npc *npc,
            struct fv_logic_player *player)
{
        npc->esperantified = true;

        logic->n_esperantified++;
        player->score++;

        if (logic->n_esperantified >= FV_PERSON_N_NPCS) {
                logic->state = FV_LOGIC_STATE_FINA_VENKO;
                logic->fina_venko_time = logic->last_ticks;
        }
}

static void
check_esperantification(struct fv_logic *logic)
{
        struct fv_logic_npc *npc;
        struct fv_logic_player *player;
        int i, j;

        if (!logic->anyone_shouting)
                return;

        for (i = 0; i < FV_PERSON_N_NPCS; i++) {
                npc = logic->npcs + i;

                if (npc->esperantified)
                        continue;

                for (j = 0; j < logic->n_players; j++) {
                        player = logic->players + j;

                        if (!player->shouting)
                                continue;

                        if (shout_in_range(player, npc)) {
                                esperantify(logic, npc, player);
                                break;
                        }
                }
        }
}

static void
update_shout_distance(struct fv_logic_player *player)
{
        if (player->shout_time >= FV_LOGIC_SHOUT_GROWTH_TIME) {
                player->shout_distance = FV_LOGIC_SHOUT_LENGTH;
        } else {
                /* Simple tweening based on a sine wave */
                player->shout_distance =
                        FV_LOGIC_SHOUT_LENGTH *
                        sinf(player->shout_time * M_PI / 2.0f /
                             FV_LOGIC_SHOUT_GROWTH_TIME);
        }
}

static void
update_shouts(struct fv_logic *logic,
              float progress_secs)
{
        struct fv_logic_player *player;
        int i;

        if (!logic->anyone_shouting)
                return;

        logic->anyone_shouting = false;

        for (i = 0; i < logic->n_players; i++) {
                player = logic->players + i;

                if (player->shouting) {
                        player->shout_time += progress_secs;

                        if (player->shout_time >=
                            (FV_LOGIC_SHOUT_LINGER_TIME +
                             FV_LOGIC_SHOUT_GROWTH_TIME)) {
                                player->shouting = false;
                        } else {
                                logic->anyone_shouting = true;

                                update_shout_distance(player);
                        }
                }
        }

        check_esperantification(logic);
}

void
fv_logic_update(struct fv_logic *logic, unsigned int ticks)
{
        unsigned int progress = ticks - logic->last_ticks;
        float progress_secs;
        int i;

        logic->last_ticks = ticks;

        /* If we've skipped over half a second then we'll assume something
         * has gone wrong and we won't do anything */
        if (progress >= 500)
                return;

        if (logic->state != FV_LOGIC_STATE_RUNNING)
                return;

        progress_secs = progress / 1000.0f;

        update_shouts(logic, progress_secs);

        for (i = 0; i < logic->n_players; i++)
                update_player_movement(logic,
                                       logic->players + i,
                                       progress_secs);

        for (i = 0; i < FV_PERSON_N_NPCS; i++)
                update_npc_movement(logic, i, progress_secs);
}

void
fv_logic_set_direction(struct fv_logic *logic,
                       int player_num,
                       float speed,
                       float direction)
{
        struct fv_logic_player *player = logic->players + player_num;

        player->position.speed = FV_LOGIC_PLAYER_SPEED * speed;
        player->position.target_direction = direction;
}

void
fv_logic_shout(struct fv_logic *logic,
               int player_num)
{
        struct fv_logic_player *player = logic->players + player_num;

        if (player->shouting)
                return;

        player->shouting = true;
        player->shout_distance = 0.0f;
        player->shout_time = 0.0f;

        logic->anyone_shouting = true;
}

void
fv_logic_free(struct fv_logic *logic)
{
        fv_free(logic);
}

void
fv_logic_get_center(struct fv_logic *logic,
                    int player_num,
                    float *x, float *y)
{
        *x = logic->players[player_num].center_x;
        *y = logic->players[player_num].center_y;
}

void
fv_logic_for_each_person(struct fv_logic *logic,
                         fv_logic_person_cb person_cb,
                         void *user_data)
{
        const struct fv_logic_player *player;
        struct fv_logic_person person;
        int i;

        person.type = FV_PERSON_TYPE_FINVENKISTO;
        person.esperantified = false;

        for (i = 0; i < logic->n_players; i++) {
                player = logic->players + i;

                person.x = player->position.x;
                person.y = player->position.y;
                person.direction = player->position.current_direction;

                person_cb(&person, user_data);
        }

        for (i = 0; i < FV_PERSON_N_NPCS; i++) {
                person.x = logic->npcs[i].position.x;
                person.y = logic->npcs[i].position.y;
                person.direction = logic->npcs[i].position.current_direction;
                person.type = fv_person_npcs[i].type;
                person.esperantified = logic->npcs[i].esperantified;

                person_cb(&person, user_data);
        }
}

void
fv_logic_for_each_shout(struct fv_logic *logic,
                        fv_logic_shout_cb shout_cb,
                        void *user_data)
{
        struct fv_logic_player *player;
        struct fv_logic_shout shout;
        int i;

        for (i = 0; i < logic->n_players; i++) {
                player = logic->players + i;

                if (!player->shouting)
                        continue;

                shout.x = player->position.x;
                shout.y = player->position.y;
                shout.direction = player->position.current_direction;
                shout.distance = player->shout_distance;
                shout_cb(&shout, user_data);
        }
}

int
fv_logic_get_n_crocodiles(struct fv_logic *logic)
{
        return FV_PERSON_N_NPCS - logic->n_esperantified;
}

int
fv_logic_get_n_players(struct fv_logic *logic)
{
        return logic->n_players;
}

int
fv_logic_get_score(struct fv_logic *logic,
                   int player_num)
{
        return logic->players[player_num].score;
}

enum fv_logic_state
fv_logic_get_state(struct fv_logic *logic)
{
        return logic->state;
}

float
fv_logic_get_time_since_fina_venko(struct fv_logic *logic)
{
        return (logic->last_ticks - logic->fina_venko_time) / 1000.0f;
}
