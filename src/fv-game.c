/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014, 2015, 2017 Neil Roberts
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
#include <float.h>
#include <stdbool.h>
#include <assert.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-map-painter.h"
#include "fv-person-painter.h"
#include "fv-shout-painter.h"
#include "fv-map.h"
#include "fv-paint-state.h"

#define FV_GAME_FRUSTUM_TOP 1.428f
/* 40° vertical FOV angle when the height of the display is
 * FV_GAME_FRUSTUM_TOP*2
 * ie, top / tan(40 / 2)
 */
#define FV_GAME_NEAR_PLANE 3.9233977549812007f
#define FV_GAME_FAR_PLANE 21.429f

#define FV_GAME_ORIGIN_DISTANCE 14.286f

struct fv_game {
        /* Size of a players viewport the last time we painted */
        int last_fb_width, last_fb_height;
        int last_n_players;

        struct fv_paint_state paint_states[FV_LOGIC_MAX_PLAYERS];

        struct fv_map_painter *map_painter;
        struct fv_person_painter *person_painter;
        struct fv_shout_painter *shout_painter;

        struct fv_matrix base_transform;
};

struct fv_game *
fv_game_new(const struct fv_vk_data *vk_data,
            struct fv_pipeline_data *pipeline_data,
            const struct fv_image_data *image_data)
{
        struct fv_game *game = fv_calloc(sizeof *game);

        fv_matrix_init_identity(&game->base_transform);

        fv_matrix_translate(&game->base_transform,
                            0.0f, 0.0f, -FV_GAME_ORIGIN_DISTANCE);

        fv_matrix_rotate(&game->base_transform,
                         -30.0f,
                         1.0f, 0.0f, 0.0f);

        game->map_painter = fv_map_painter_new(&fv_map,
                                               vk_data,
                                               pipeline_data,
                                               image_data);
        if (game->map_painter == NULL)
                goto error;

        game->person_painter = fv_person_painter_new(vk_data,
                                                     pipeline_data,
                                                     image_data);
        if (game->person_painter == NULL)
                goto error_map_painter;

        game->shout_painter = fv_shout_painter_new(vk_data,
                                                   pipeline_data,
                                                   image_data);
        if (game->shout_painter == NULL)
                goto error_person_painter;

        return game;

error_person_painter:
        fv_person_painter_free(game->person_painter);
error_map_painter:
        fv_map_painter_free(game->map_painter);
error:
        fv_free(game);

        return NULL;
}

static void
update_visible_area(struct fv_game *game)
{
        struct fv_matrix m, inverse;
        float min_x = FLT_MAX, max_x = -FLT_MAX;
        float min_y = FLT_MAX, max_y = -FLT_MAX;
        float points_in[4 * 2 * 3], points_out[4 * 2 * 4];
        float *p = points_in;
        int x, y, z, i;
        float px, py, frac;
        float visible_w, visible_h;

        fv_matrix_multiply(&m,
                           &game->paint_states[0].transform.projection,
                           &game->base_transform);
        fv_matrix_get_inverse(&m, &inverse);

        for (y = -1; y <= 1; y += 2) {
                for (x = -1; x <= 1; x += 2) {
                        *(p++) = x;
                        *(p++) = y;
                        *(p++) = -1.0f;
                        *(p++) = x;
                        *(p++) = y;
                        *(p++) = 1.0f;
                }
        }

        fv_matrix_project_points(&inverse,
                                 3, /* n_components */
                                 sizeof (float) * 3,
                                 points_in,
                                 sizeof (float) * 4,
                                 points_out,
                                 4 * 2 /* n_points */);

        for (i = 0; i < 4 * 2; i++) {
                points_out[i * 4 + 0] /= points_out[i * 4 + 3];
                points_out[i * 4 + 1] /= points_out[i * 4 + 3];
                points_out[i * 4 + 2] /= points_out[i * 4 + 3];
        }

        for (i = 0; i < 4; i++) {
                /* The two unprojected points represent a line going
                 * from the near plane to the far plane which gets
                 * projected to a single point touching one of the
                 * corners of the viewport. Here we work out the x/y
                 * position of the point along the line where it
                 * touches the plane representing the floor and the
                 * ceiling of the world and keep track of the furthest
                 * one. */
                for (z = 0; z <= 2; z += 2) {
                        p = points_out + i * 4 * 2;
                        frac = (z - p[6]) / (p[2] - p[6]);
                        px = frac * (p[0] - p[4]) + p[4];
                        py = frac * (p[1] - p[5]) + p[5];

                        if (px < min_x)
                                min_x = px;
                        if (px > max_x)
                                max_x = px;
                        if (py < min_y)
                                min_y = py;
                        if (py > max_y)
                                max_y = py;
                }
        }

        visible_w = fmaxf(fabsf(min_x), fabsf(max_x)) * 2.0f + 1.0f;
        visible_h = fmaxf(fabsf(min_y), fabsf(max_y)) * 2.0f + 1.0f;

        for (i = 0; i < game->last_n_players; i++) {
                game->paint_states[i].visible_w = visible_w;
                game->paint_states[i].visible_h = visible_h;
        }
}

static void
update_viewports(struct fv_game *game)
{
        int viewport_width, viewport_height;
        int n_viewports = game->last_n_players;
        int i;

        viewport_width = game->last_fb_width;
        viewport_height = game->last_fb_height;

        if (n_viewports > 1) {
                viewport_width /= 2;
                if (n_viewports > 2)
                        viewport_height /= 2;
        }

        for (i = 0; i < n_viewports; i++) {
                game->paint_states[i].viewport_x = i % 2 * viewport_width;
                game->paint_states[i].viewport_y = i / 2 * viewport_height;
                game->paint_states[i].viewport_width = viewport_width;
                game->paint_states[i].viewport_height = viewport_height;
        }
}

static void
update_projection(struct fv_game *game)
{
        float right, top;
        int w = game->paint_states[0].viewport_width;
        int h = game->paint_states[0].viewport_height;
        struct fv_matrix projection;
        int i;

        if (w == 0 || h == 0)
                w = h = 1;

        if (w < h) {
                right = FV_GAME_FRUSTUM_TOP;
                top = h * FV_GAME_FRUSTUM_TOP / (float) w;
        } else {
                top = FV_GAME_FRUSTUM_TOP;
                right = w * FV_GAME_FRUSTUM_TOP / (float) h;
        }

        fv_matrix_init_identity(&projection);

        fv_matrix_frustum(&projection,
                          -right, right,
                          top, -top,
                          FV_GAME_NEAR_PLANE,
                          FV_GAME_FAR_PLANE);

        for (i = 0; i < game->last_n_players; i++) {
                game->paint_states[i].transform.projection = projection;

                fv_transform_dirty(&game->paint_states[i].transform);
        }
}

static void
update_centers(struct fv_game *game,
               struct fv_logic *logic)
{
        int n_viewports;
        int i;

        if (fv_logic_get_state(logic) == FV_LOGIC_STATE_NO_PLAYERS) {
                for (i = 0; i < game->last_n_players; i++) {
                        game->paint_states[i].center_x = FV_MAP_START_X;
                        game->paint_states[i].center_y = FV_MAP_START_Y;
                }
        } else {
                n_viewports = fv_logic_get_n_players(logic);

                for (i = 0; i < n_viewports; i++) {
                        fv_logic_get_center(logic,
                                            i,
                                            &game->paint_states[i].center_x,
                                            &game->paint_states[i].center_y);
                }
        }
}

static void
update_modelview(struct fv_game *game,
                 struct fv_logic *logic,
                 struct fv_paint_state *paint_state)
{
        paint_state->transform.modelview = game->base_transform;

        fv_matrix_translate(&paint_state->transform.modelview,
                            -paint_state->center_x,
                            -paint_state->center_y,
                            0.0f);

        fv_transform_dirty(&paint_state->transform);
}

void
fv_game_update_fb_size(struct fv_game *game,
                       int width, int height,
                       int n_players)
{
        if (width == game->last_fb_width &&
            height == game->last_fb_height &&
            n_players == game->last_n_players)
                return;

        game->last_fb_width = width;
        game->last_fb_height = height;
        game->last_n_players = n_players;

        update_viewports(game);
        update_projection(game);
        update_visible_area(game);
}

bool
fv_game_covers_framebuffer(struct fv_game *game)
{
        float visible_w, visible_h;
        float center_x, center_y;
        int i;

        for (i = 0; i < game->last_n_players; i++) {
                visible_w = game->paint_states[i].visible_w;
                visible_h = game->paint_states[i].visible_h;
                center_x = game->paint_states[i].center_x;
                center_y = game->paint_states[i].center_y;

                /* We only need to clear if the map doesn't cover the
                 * entire viewport */
                if (center_x - visible_w / 2.0f < 0.0f ||
                    center_y - visible_h / 2.0f < 0.0f ||
                    center_x + visible_w / 2.0f > FV_MAP_WIDTH ||
                    center_y + visible_h / 2.0f > FV_MAP_HEIGHT)
                        return false;
        }

        return true;
}

void
fv_game_paint(struct fv_game *game,
              struct fv_logic *logic,
              VkCommandBuffer command_buffer)
{
        struct fv_paint_state *paint_state;
        int i;

        update_centers(game, logic);

        for (i = 0; i < game->last_n_players; i++)
                update_modelview(game, logic, game->paint_states + i);

        if (game->last_n_players == 1) {
                VkViewport viewport = {
                        .x = game->paint_states[0].viewport_x,
                        .y = game->paint_states[0].viewport_y,
                        .width = game->paint_states[0].viewport_width,
                        .height = game->paint_states[0].viewport_height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                };
                fv_vk.vkCmdSetViewport(command_buffer,
                                       0, /* firstViewport */
                                       1, /* viewportCount */
                                       &viewport);
        }

        fv_map_painter_paint(game->map_painter,
                             command_buffer,
                             game->last_n_players,
                             game->paint_states);
        fv_person_painter_paint(game->person_painter,
                                logic,
                                command_buffer,
                                game->last_n_players,
                                game->paint_states);

        fv_shout_painter_begin_frame(game->shout_painter);

        for (i = 0; i < game->last_n_players; i++) {
                if (game->last_n_players != 1) {
                        VkViewport viewport = {
                                .x = game->paint_states[i].viewport_x,
                                .y = game->paint_states[i].viewport_y,
                                .width = game->paint_states[i].viewport_width,
                                .height = game->paint_states[i].viewport_height,
                                .minDepth = 0.0f,
                                .maxDepth = 1.0f
                        };
                        fv_vk.vkCmdSetViewport(command_buffer,
                                               0, /* firstViewport */
                                               1, /* viewportCount */
                                               &viewport);
                }

                paint_state = game->paint_states + i;

                fv_shout_painter_paint(game->shout_painter,
                                       logic,
                                       command_buffer,
                                       paint_state);
        }

        fv_shout_painter_end_frame(game->shout_painter);
}

void
fv_game_free(struct fv_game *game)
{
        fv_shout_painter_free(game->shout_painter);
        fv_person_painter_free(game->person_painter);
        fv_map_painter_free(game->map_painter);
        fv_free(game);
}
