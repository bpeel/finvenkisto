/*
 * Finvenkisto
 *
 * Copyright (C) 2017 Neil Roberts
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

#ifndef FV_INPUT_H
#define FV_INPUT_H

#include <stdbool.h>

#include "fv-logic.h"

enum fv_input_state {
        FV_INPUT_STATE_CHOOSING_N_PLAYERS,
        FV_INPUT_STATE_CHOOSING_CONTROLLERS,
        FV_INPUT_STATE_PLAYING
};

typedef void
(* fv_input_state_changed_cb)(void *user_data);

struct fv_input *
fv_input_new(struct fv_logic *logic);

void
fv_input_set_state_changed_cb(struct fv_input *input,
                              fv_input_state_changed_cb cb,
                              void *user_data);

enum fv_input_state
fv_input_get_state(struct fv_input *input);

int
fv_input_get_n_players(struct fv_input *input);

int
fv_input_get_next_player(struct fv_input *input);

bool
fv_input_handle_event(struct fv_input *input,
                      const SDL_Event *event);

void
fv_input_reset(struct fv_input *input);

void
fv_input_free(struct fv_input *input);

#endif /* FV_INPUT_H */
