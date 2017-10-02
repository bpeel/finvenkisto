/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2015, 2017 Neil Roberts
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

#ifndef FV_HUD_H
#define FV_HUD_H

#include "fv-logic.h"
#include "fv-image-data.h"
#include "fv-pipeline-data.h"
#include "fv-vk-data.h"

struct fv_hud *
fv_hud_new(const struct fv_vk_data *vk_data,
           const struct fv_pipeline_data *pipeline_data,
           const struct fv_image_data *image_data);

void
fv_hud_paint_player_select(struct fv_hud *hud,
                           VkCommandBuffer command_buffer,
                           int n_players,
                           int screen_width,
                           int screen_height);

void
fv_hud_paint_controller_select(struct fv_hud *hud,
                               VkCommandBuffer command_buffer,
                               int screen_width,
                               int screen_height,
                               int player_num,
                               int n_players);

void
fv_hud_paint_game_state(struct fv_hud *hud,
                        VkCommandBuffer command_buffer,
                        int screen_width,
                        int screen_height,
                        struct fv_logic *logic);

void
fv_hud_free(struct fv_hud *hud);

#endif /* FV_HUD_H */
