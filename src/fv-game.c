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
#include <SDL.h>
#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <assert.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"

#define FV_GAME_NEAR_PLANE 1.0f
#define FV_GAME_FAR_PLANE 10.0f

struct fv_game {
        /* Size of the framebuffer the last time we painted */
        int last_fb_width, last_fb_height;

        struct fv_transform transform;
};

struct fv_game *
fv_game_new(struct fv_shader_data *shader_data)
{
        struct fv_game *game = fv_calloc(sizeof *game);

        return game;
}

static void
update_projection(struct fv_game *game,
                  int w, int h)
{
        if (w == 0 || h == 0)
                w = h = 1;

        /* Recalculate the projection matrix if we've got a different size
         * from last time */
        if (w != game->last_fb_width || h != game->last_fb_height) {
                fv_matrix_init_identity(&game->transform.projection);

                fv_matrix_frustum(&game->transform.projection,
                                  -1.0f, 1.0f,
                                  -1.0f, 1.0f,
                                  FV_GAME_NEAR_PLANE,
                                  FV_GAME_FAR_PLANE);

                game->last_fb_width = w;
                game->last_fb_height = h;
        }
}

static void
update_modelview(struct fv_game *game)
{
        fv_matrix_init_identity(&game->transform.modelview);

        fv_transform_update_derived_values(&game->transform);
}

void
fv_game_paint(struct fv_game *game,
              int width, int height,
              struct fv_logic *logic)
{
        update_projection(game, width, height);

        update_modelview(game);
}

void
fv_game_free(struct fv_game *game)
{
        fv_free(game);
}
