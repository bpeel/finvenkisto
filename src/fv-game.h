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

#ifndef FV_GAME_H
#define FV_GAME_H

#include <stdbool.h>

#include "fv-logic.h"
#include "fv-shader-data.h"

struct fv_game *
fv_game_new(struct fv_shader_data *shader_data);

void
fv_game_paint(struct fv_game *game,
              int width, int height,
              struct fv_logic *logic);

void
fv_game_free(struct fv_game *game);

#endif /* FV_GAME_H */
