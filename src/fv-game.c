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

#include <epoxy/gl.h>
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

#define FV_GAME_NEAR_PLANE 1.0f
#define FV_GAME_FAR_PLANE 10.0f

#define FV_GAME_ORIGIN_DISTANCE 5.0f
#define FV_GAME_SCALE 0.5f

struct fv_game {
        /* Size of the framebuffer the last time we painted */
        int last_fb_width, last_fb_height;
        int visible_w, visible_h;

        struct fv_map_painter *map_painter;

        struct fv_transform transform;

        struct fv_matrix base_transform;
};

struct fv_game *
fv_game_new(struct fv_shader_data *shader_data)
{
        struct fv_game *game = fv_calloc(sizeof *game);

        fv_matrix_init_identity(&game->base_transform);

        fv_matrix_translate(&game->base_transform,
                            0.0f, 0.0f, -FV_GAME_ORIGIN_DISTANCE);

        fv_matrix_scale(&game->base_transform,
                        FV_GAME_SCALE, FV_GAME_SCALE, FV_GAME_SCALE);

        game->map_painter = fv_map_painter_new(shader_data);

        if (game->map_painter == NULL)
                goto error;

        return game;

error:
        fv_free(game);

        return NULL;
}

static void
update_projection(struct fv_game *game,
                  int w, int h)
{
        float right, top;

        if (w == 0 || h == 0)
                w = h = 1;

        /* Recalculate the projection matrix if we've got a different size
         * from last time */
        if (w != game->last_fb_width || h != game->last_fb_height) {
                if (w < h) {
                        right = 1.0f;
                        top = h / (float) w;
                } else {
                        top = 1.0f;
                        right = w / (float) h;
                }

                fv_matrix_init_identity(&game->transform.projection);

                fv_matrix_frustum(&game->transform.projection,
                                  -right, right,
                                  -top, top,
                                  FV_GAME_NEAR_PLANE,
                                  FV_GAME_FAR_PLANE);

                game->visible_w = (FV_GAME_ORIGIN_DISTANCE /
                                   FV_GAME_NEAR_PLANE *
                                   right * 2.0f /
                                   FV_GAME_SCALE) + 1.0f;
                game->visible_h = (FV_GAME_ORIGIN_DISTANCE /
                                   FV_GAME_NEAR_PLANE *
                                   top * 2.0f /
                                   FV_GAME_SCALE) + 1.0f;

                game->last_fb_width = w;
                game->last_fb_height = h;
        }
}

static void
update_modelview(struct fv_game *game,
                 struct fv_logic *logic)
{
        float center_x, center_y;

        game->transform.modelview = game->base_transform;

        fv_logic_get_center(logic, &center_x, &center_y);
        fv_matrix_translate(&game->transform.modelview,
                            -center_x, -center_y, 0.0f);
}

void
fv_game_paint(struct fv_game *game,
              int width, int height,
              struct fv_logic *logic)
{
        update_projection(game, width, height);

        update_modelview(game, logic);

        fv_transform_update_derived_values(&game->transform);

        fv_map_painter_paint(game->map_painter,
                             logic,
                             game->visible_w, game->visible_h,
                             &game->transform);
}

void
fv_game_free(struct fv_game *game)
{
        fv_map_painter_free(game->map_painter);
        fv_free(game);
}
