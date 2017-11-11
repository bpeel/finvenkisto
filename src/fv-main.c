/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014, 2015, 2016, 2017 Neil Roberts
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

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <SDL.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-image-data.h"
#include "fv-vk.h"
#include "fv-util.h"
#include "fv-hud.h"
#include "fv-map.h"
#include "fv-data.h"
#include "fv-pipeline-data.h"
#include "fv-vk-data.h"
#include "fv-input.h"
#include "fv-window.h"

struct viewport {
        int x, y;
        int width, height;
        float center_x, center_y;
};

struct data {
        struct fv_window *window;
        struct fv_vk_data *vk_data;

        struct {
                struct fv_game *game;
                struct fv_hud *hud;
        } graphics;

        struct fv_pipeline_data pipeline_data;

        struct fv_logic *logic;

        bool quit;

        bool viewports_dirty;
        int n_viewports;

        Uint32 start_time;

        struct fv_input *input;

        int last_fb_width, last_fb_height;
        struct viewport viewports[FV_LOGIC_MAX_PLAYERS];

        bool fullscreen_opt;
};

static void
reset_start_time(struct data *data)
{
        data->start_time = SDL_GetTicks();
}

static unsigned int
get_ticks(struct data *data)
{
        return SDL_GetTicks() - data->start_time;
}

static void
reset_menu_state(struct data *data)
{
        reset_start_time(data);
        data->viewports_dirty = true;
        data->n_viewports = 1;

        fv_input_reset(data->input);
        fv_logic_reset(data->logic, 0);
}

static bool
handle_key_event(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        switch (event->keysym.sym) {
        case SDLK_ESCAPE:
                if (event->state == SDL_PRESSED) {
                        switch (fv_input_get_state(data->input)) {
                        case FV_INPUT_STATE_CHOOSING_N_PLAYERS:
                                data->quit = true;
                                break;
                        default:
                                reset_menu_state(data);
                                break;
                        }
                        return true;
                }
                break;

        case SDLK_F11:
                if (event->state == SDL_PRESSED) {
                        fv_window_toggle_fullscreen(data->window);
                        return true;
                }
                break;
        }

        return false;
}

static void
input_state_changed_cb(void *user_data)
{
        struct data *data = user_data;

        if (fv_input_get_state(data->input) == FV_INPUT_STATE_PLAYING) {
                reset_start_time(data);
                fv_logic_reset(data->logic,
                               fv_input_get_n_players(data->input));
        }

        data->viewports_dirty = true;
}

static void
destroy_graphics(struct data *data)
{
        if (data->graphics.game) {
                fv_game_free(data->graphics.game);
                data->graphics.game = NULL;
        }

        if (data->graphics.hud) {
                fv_hud_free(data->graphics.hud);
                data->graphics.hud = NULL;
        }
}

static bool
create_graphics(struct data *data)
{
        VkCommandBuffer command_buffer;
        struct fv_image_data *image_data;
        VkResult res;

        VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = data->vk_data->command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
        };
        res = fv_vk.vkAllocateCommandBuffers(data->vk_data->device,
                                             &command_buffer_allocate_info,
                                             &command_buffer);
        if (res != VK_SUCCESS)
                return false;

        VkCommandBufferBeginInfo command_buffer_begin_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        fv_vk.vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

        image_data = fv_image_data_new(data->vk_data, command_buffer);
        if (image_data == NULL)
                goto error_command_buffer;

        memset(&data->graphics, 0, sizeof data->graphics);

        data->graphics.hud = fv_hud_new(data->vk_data,
                                        &data->pipeline_data,
                                        image_data);

        if (data->graphics.hud == NULL)
                goto error;

        data->graphics.game = fv_game_new(data->vk_data,
                                          &data->pipeline_data,
                                          image_data);

        if (data->graphics.game == NULL)
                goto error;

        fv_vk.vkEndCommandBuffer(command_buffer);

        VkSubmitInfo submitInfo = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &command_buffer
        };
        fv_vk.vkQueueSubmit(data->vk_data->queue,
                            1,
                            &submitInfo,
                            VK_NULL_HANDLE);

        fv_vk.vkQueueWaitIdle(data->vk_data->queue);

        fv_image_data_free(image_data);

        fv_vk.vkFreeCommandBuffers(data->vk_data->device,
                                   data->vk_data->command_pool,
                                   1, /* commandBufferCount */
                                   &command_buffer);

        return true;

error:
        destroy_graphics(data);
        fv_image_data_free(image_data);
error_command_buffer:
        fv_vk.vkFreeCommandBuffers(data->vk_data->device,
                                   data->vk_data->command_pool,
                                   1, /* commandBufferCount */
                                   &command_buffer);
        return false;
}

static void
handle_event(struct data *data,
             const SDL_Event *event)
{
        switch (event->type) {
        case SDL_WINDOWEVENT:
                switch (event->window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                        data->quit = true;
                        break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                        fv_window_resized(data->window);
                        break;
                }
                goto handled;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
                if (handle_key_event(data, &event->key))
                        goto handled;
                break;

        case SDL_QUIT:
                data->quit = true;
                goto handled;
        }

        if (fv_input_handle_event(data->input, event))
                goto handled;

handled:
        (void) 0;
}

static void
paint_hud(struct data *data,
          int w, int h)
{
        int n_players = fv_input_get_n_players(data->input);
        int next_player = fv_input_get_next_player(data->input);

        switch (fv_input_get_state(data->input)) {
        case FV_INPUT_STATE_CHOOSING_N_PLAYERS:
                fv_hud_paint_player_select(data->graphics.hud,
                                           data->vk_data->command_buffer,
                                           n_players,
                                           w, h);
                break;
        case FV_INPUT_STATE_CHOOSING_CONTROLLERS:
                fv_hud_paint_controller_select(data->graphics.hud,
                                               data->vk_data->command_buffer,
                                               w, h,
                                               next_player,
                                               n_players);
                break;
        case FV_INPUT_STATE_PLAYING:
                fv_hud_paint_game_state(data->graphics.hud,
                                        data->vk_data->command_buffer,
                                        w, h,
                                        data->logic);
                break;
        }
}

static void
update_viewports(struct data *data)
{
        int viewport_width, viewport_height;
        int i;

        if (!data->viewports_dirty)
                return;

        if (fv_input_get_state(data->input) ==
            FV_INPUT_STATE_CHOOSING_N_PLAYERS)
                data->n_viewports = 1;
        else
                data->n_viewports = fv_input_get_n_players(data->input);

        viewport_width = data->last_fb_width;
        viewport_height = data->last_fb_height;

        if (data->n_viewports > 1) {
                viewport_width /= 2;
                if (data->n_viewports > 2)
                        viewport_height /= 2;
        }

        for (i = 0; i < data->n_viewports; i++) {
                data->viewports[i].x = i % 2 * viewport_width;
                data->viewports[i].y = i / 2 * viewport_height;
                data->viewports[i].width = viewport_width;
                data->viewports[i].height = viewport_height;
        }

        data->viewports_dirty = false;
}

static void
update_centers(struct data *data)
{
        int i;

        if (fv_input_get_state(data->input) == FV_INPUT_STATE_PLAYING) {
                for (i = 0; i < data->n_viewports; i++) {
                        fv_logic_get_center(data->logic,
                                            i,
                                            &data->viewports[i].center_x,
                                            &data->viewports[i].center_y);
                }
        } else {
                for (i = 0; i < data->n_viewports; i++) {
                        data->viewports[i].center_x = FV_MAP_START_X;
                        data->viewports[i].center_y = FV_MAP_START_Y;
                }
        }
}

static bool
need_clear(struct data *data)
{
        const struct viewport *viewport;
        int i;

        /* If there are only 3 divisions then one of the panels will
         * be blank so we always need to clear */
        if (data->n_viewports == 3)
                return true;

        /* If the window is an odd size then the divisions might not
         * cover the entire window */
        if (data->n_viewports >= 2) {
                if (data->last_fb_width & 1)
                        return true;
                if (data->n_viewports >= 3 && (data->last_fb_height & 1))
                        return true;
        }

        /* Otherwise check if all of the divisions currently cover
         * their visible area */
        for (i = 0; i < data->n_viewports; i++) {
                viewport = data->viewports + i;
                if (!fv_game_covers_framebuffer(data->graphics.game,
                                                viewport->center_x,
                                                viewport->center_y,
                                                viewport->width,
                                                viewport->height))
                        return true;
        }

        return false;
}

static void
clear_window(struct data *data,
             const VkExtent2D *extent)
{
        VkClearAttachment color_clear_attachment = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = 0,
                .clearValue = {
                        .color = {
                                .float32 = { 0.0f, 0.0f, 0.0f, 0.0f }
                        }
                },
        };
        VkClearRect color_clear_rect = {
                .rect = {
                        .offset = { 0, 0 },
                        .extent = *extent
                },
                .baseArrayLayer = 0,
                .layerCount = 1
        };
        fv_vk.vkCmdClearAttachments(data->vk_data->command_buffer,
                                    1, /* attachmentCount */
                                    &color_clear_attachment,
                                    1,
                                    &color_clear_rect);
}

static void
paint(struct data *data)
{
        VkExtent2D extent;
        int i;

        if (!fv_window_begin_paint(data->window)) {
                data->quit = true;
                return;
        }

        fv_window_get_extent(data->window, &extent);

        if (extent.width != data->last_fb_width ||
            extent.height != data->last_fb_height) {
                data->last_fb_width = extent.width;
                data->last_fb_height = extent.height;
                data->viewports_dirty = true;
        }

        if (need_clear(data))
                clear_window(data, &extent);

        fv_logic_update(data->logic, get_ticks(data));

        update_viewports(data);
        update_centers(data);

        fv_game_begin_frame(data->graphics.game);

        for (i = 0; i < data->n_viewports; i++) {
                VkViewport viewport = {
                        .x = data->viewports[i].x,
                        .y = data->viewports[i].y,
                        .width = data->viewports[i].width,
                        .height = data->viewports[i].height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                };
                fv_vk.vkCmdSetViewport(data->vk_data->command_buffer,
                                       0, /* firstViewport */
                                       1, /* viewportCount */
                                       &viewport);
                fv_game_paint(data->graphics.game,
                              data->viewports[i].center_x,
                              data->viewports[i].center_y,
                              data->viewports[i].width,
                              data->viewports[i].height,
                              data->logic,
                              data->vk_data->command_buffer);
        }

        fv_game_end_frame(data->graphics.game);

        if (data->n_viewports != 1) {
                VkViewport viewport = {
                        .x = 0,
                        .y = 0,
                        .width = extent.width,
                        .height = extent.height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                };
                fv_vk.vkCmdSetViewport(data->vk_data->command_buffer,
                                       0, /* firstViewport */
                                       1, /* viewportCount */
                                       &viewport);
        }

        paint_hud(data, extent.width, extent.height);

        if (!fv_window_end_paint(data->window))
                data->quit = true;
}

static void
show_help(void)
{
        printf("Finvenkisto - Instruludo por venigi la finan venkon\n"
               "uzo: finvenkisto [opcioj]\n"
               "Opcioj:\n"
               " -h       Montru ĉi tiun helpmesaĝon\n"
               " -f       Rulu la ludon en fenestro\n"
               " -p       Rulu la ludon plenekrane (defaŭlto)\n");
}

static bool
process_argument_flags(struct data *data,
                       const char *flags)
{
        while (*flags) {
                switch (*flags) {
                case 'h':
                        show_help();
                        return false;

                case 'f':
                        data->fullscreen_opt = false;
                        break;

                case 'p':
                        data->fullscreen_opt = true;
                        break;

                default:
                        fprintf(stderr, "Neatendita opcio ‘%c’\n", *flags);
                        show_help();
                        return false;
                }

                flags++;
        }

        return true;
}

static bool
process_arguments(struct data *data,
                  int argc, char **argv)
{
        int i;

        for (i = 1; i < argc; i++) {
                if (argv[i][0] == '-') {
                        if (!process_argument_flags(data, argv[i] + 1))
                                return false;
                } else {
                        fprintf(stderr, "Neatendita argumento ‘%s’\n", argv[i]);
                        show_help();
                        return false;
                }
        }

        return true;
}

static void
iterate_main_loop(struct data *data)
{
        SDL_Event event;

        if (SDL_PollEvent(&event)) {
                handle_event(data, &event);
                return;
        }

        paint(data);
}

int
main(int argc, char **argv)
{
        struct data *data = fv_calloc(sizeof *data);
        int ret = EXIT_SUCCESS;

        data->fullscreen_opt = true;

        fv_data_init(argv[0]);

        if (!process_arguments(data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out_data;
        }

        data->window = fv_window_new(data->fullscreen_opt);
        if (data->window == NULL)
                goto out_data;

        data->vk_data = fv_window_get_vk_data(data->window);

        data->quit = false;

        data->logic = fv_logic_new();
        data->input = fv_input_new(data->logic);
        fv_input_set_state_changed_cb(data->input,
                                      input_state_changed_cb,
                                      data);

        if (!fv_pipeline_data_init(data->vk_data,
                                   data->vk_data->render_pass,
                                   &data->pipeline_data))
                goto out_input;

        if (!create_graphics(data)) {
                ret = EXIT_FAILURE;
                goto out_pipeline_data;
        }

        reset_menu_state(data);

        while (!data->quit)
                iterate_main_loop(data);

        destroy_graphics(data);

out_pipeline_data:
        fv_pipeline_data_destroy(data->vk_data,
                                 &data->pipeline_data);
out_input:
        fv_input_free(data->input);
        fv_logic_free(data->logic);
        fv_window_free(data->window);
out_data:
        fv_data_deinit();
        fv_free(data);

        return ret;
}
