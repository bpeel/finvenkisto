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

#include "fv-image-data.h"
#include "fv-vk.h"
#include "fv-util.h"
#include "fv-map.h"
#include "fv-data.h"
#include "fv-pipeline-data.h"
#include "fv-vk-data.h"
#include "fv-window.h"
#include "fv-map-painter.h"
#include "fv-highlight-painter.h"

#define FV_EDITOR_FRUSTUM_TOP 1.428f
/* 40° vertical FOV angle when the height of the display is
 * FV_EDITOR_FRUSTUM_TOP*2
 * ie, top / tan(40 / 2)
 */
#define FV_EDITOR_NEAR_PLANE 3.9233977549812007f
#define FV_EDITOR_FAR_PLANE 57.143f

#define FV_EDITOR_MIN_DISTANCE 14.286f
#define FV_EDITOR_MAX_DISTANCE 42.857f

struct data {
        struct fv_window *window;
        struct fv_vk_data *vk_data;

        struct fv_pipeline_data pipeline_data;

        struct fv_map_painter *map_painter;
        struct fv_highlight_painter *highlight_painter;

        bool quit;
        bool redraw_queued;

        bool fullscreen_opt;

        int x_pos;
        int y_pos;
        int distance;
        int rotation;
};

static void
queue_redraw(struct data *data)
{
        data->redraw_queued = true;
}

static void
update_position(struct data *data,
                int x_offset,
                int y_offset)
{
        int t;

        switch (data->rotation) {
        case 1:
                t = x_offset;
                x_offset = y_offset;
                y_offset = -t;
                break;

        case 2:
                x_offset = -x_offset;
                y_offset = -y_offset;
                break;

        case 3:
                t = x_offset;
                x_offset = -y_offset;
                y_offset = t;
                break;
        }

        data->x_pos += x_offset;
        data->y_pos += y_offset;

        if (data->x_pos < 0)
                data->x_pos = 0;
        else if (data->x_pos >= FV_MAP_WIDTH)
                data->x_pos = FV_MAP_WIDTH - 1;

        if (data->y_pos < 0)
                data->y_pos = 0;
        else if (data->y_pos >= FV_MAP_HEIGHT)
                data->y_pos = FV_MAP_HEIGHT - 1;

        queue_redraw(data);
}

static void
update_distance(struct data *data,
                int offset)
{
        data->distance += offset;

        if (data->distance > FV_EDITOR_MAX_DISTANCE)
                data->distance = FV_EDITOR_MAX_DISTANCE;
        else if (data->distance < FV_EDITOR_MIN_DISTANCE)
                data->distance = FV_EDITOR_MIN_DISTANCE;

        queue_redraw(data);
}

static bool
handle_key_event(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        if (event->state != SDL_PRESSED)
                return false;

        switch (event->keysym.sym) {
        case SDLK_ESCAPE:
                data->quit = true;
                return true;

        case SDLK_F11:
                fv_window_toggle_fullscreen(data->window);
                return true;

        case SDLK_LEFT:
                update_position(data, -1, 0);
                return true;

        case SDLK_RIGHT:
                update_position(data, 1, 0);
                return true;

        case SDLK_DOWN:
                update_position(data, 0, -1);
                return true;

        case SDLK_UP:
                update_position(data, 0, 1);
                return true;

        case SDLK_a:
                update_distance(data, -1);
                return true;

        case SDLK_z:
                update_distance(data, 1);
                return true;

        case SDLK_r:
                data->rotation = (data->rotation + 1) % 4;
                queue_redraw(data);
                return true;
        }

        return false;
}

static void
destroy_graphics(struct data *data)
{
        if (data->map_painter) {
                fv_map_painter_free(data->map_painter);
                data->map_painter = NULL;
        }

        if (data->highlight_painter) {
                fv_highlight_painter_free(data->highlight_painter);
                data->highlight_painter = NULL;
        }
}

static bool
create_graphics(struct data *data)
{
        VkCommandBuffer command_buffer = NULL;
        struct fv_image_data *image_data = NULL;
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
        if (res != VK_SUCCESS) {
                command_buffer = NULL;
                goto error;
        }

        VkCommandBufferBeginInfo command_buffer_begin_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        fv_vk.vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

        image_data = fv_image_data_new(data->vk_data, command_buffer);
        if (image_data == NULL)
                goto error;

        data->map_painter = fv_map_painter_new(data->vk_data,
                                               &data->pipeline_data,
                                               image_data);
        if (data->map_painter == NULL)
                goto error;

        data->highlight_painter =
                fv_highlight_painter_new(data->vk_data,
                                         &data->pipeline_data);
        if (data->highlight_painter == NULL)
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

        if (image_data)
                fv_image_data_free(image_data);
        if (command_buffer) {
                fv_vk.vkFreeCommandBuffers(data->vk_data->device,
                                           data->vk_data->command_pool,
                                           1, /* commandBufferCount */
                                           &command_buffer);
        }

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
                        /* flow through */
                case SDL_WINDOWEVENT_EXPOSED:
                        queue_redraw(data);
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

handled:
        (void) 0;
}

static void
draw_highlights(struct data *data,
                struct fv_paint_state *paint_state)
{
        int block_pos = data->x_pos + data->y_pos * FV_MAP_WIDTH;
        float z_pos;

        switch (FV_MAP_GET_BLOCK_TYPE(fv_map.blocks[block_pos])) {
        case FV_MAP_BLOCK_TYPE_FULL_WALL:
                z_pos = 2.1f;
                break;
        case FV_MAP_BLOCK_TYPE_HALF_WALL:
                z_pos = 1.1f;
                break;
        default:
                z_pos = 0.1f;
                break;
        }

        struct fv_highlight_painter_highlight highlight = {
                .x = data->x_pos,
                .y = data->y_pos,
                .z = z_pos,
                .w = 1.0f,
                .h = 1.0f,
                .r = 0.75f * 0.8f * 255.0f,
                .g = 0.75f * 0.8f * 255.0f,
                .b = 1.00f * 0.8f * 255.0f,
                .a = 0.8f * 255.0f
        };
        fv_highlight_painter_paint(data->highlight_painter,
                                   data->vk_data->command_buffer,
                                   1, /* n_highlights */
                                   &highlight,
                                   paint_state);
}

static void
paint(struct data *data)
{
        struct fv_paint_state paint_state;
        struct fv_transform *transform = &paint_state.transform;
        float right, top;
        VkExtent2D extent;

        if (!fv_window_begin_paint(data->window,
                                   true /* need_clear */)) {
                data->quit = true;
                return;
        }

        fv_window_get_extent(data->window, &extent);

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

        paint_state.center_x = data->x_pos + 0.5f;
        paint_state.center_y = data->y_pos + 0.5f;
        paint_state.visible_w = FV_MAP_WIDTH * 8.0f;
        paint_state.visible_h = FV_MAP_HEIGHT * 8.0f;

        if (extent.width < extent.height) {
                right = FV_EDITOR_FRUSTUM_TOP;
                top = (extent.height * FV_EDITOR_FRUSTUM_TOP /
                       (float) extent.width);
        } else {
                top = FV_EDITOR_FRUSTUM_TOP;
                right = (extent.width * FV_EDITOR_FRUSTUM_TOP /
                         (float) extent.height);
        }

        fv_matrix_init_identity(&transform->projection);

        fv_matrix_frustum(&transform->projection,
                          -right, right,
                          top, -top,
                          FV_EDITOR_NEAR_PLANE,
                          FV_EDITOR_FAR_PLANE);

        fv_matrix_init_identity(&transform->modelview);

        fv_matrix_translate(&transform->modelview,
                            0.0f, 0.0f, -data->distance);

        fv_matrix_rotate(&transform->modelview,
                         -30.0f,
                         1.0f, 0.0f, 0.0f);

        fv_matrix_rotate(&transform->modelview,
                         data->rotation * 90.0f,
                         0.0f, 0.0f, 1.0f);

        fv_matrix_translate(&transform->modelview,
                            -paint_state.center_x,
                            -paint_state.center_y,
                            0.0f);

        fv_transform_dirty(&paint_state.transform);

        fv_map_painter_begin_frame(data->map_painter);
        fv_highlight_painter_begin_frame(data->highlight_painter);

        fv_map_painter_paint(data->map_painter,
                             data->vk_data->command_buffer,
                             &paint_state);

        draw_highlights(data, &paint_state);

        fv_highlight_painter_end_frame(data->highlight_painter);
        fv_map_painter_end_frame(data->map_painter);

        if (!fv_window_end_paint(data->window))
                data->quit = true;
}

static void
handle_redraw(struct data *data)
{
        paint(data);
        data->redraw_queued = false;
}

static void
show_help(void)
{
        printf("Finvenkisto - Instruludo por venigi la finan venkon\n"
               "uzo: finvenkisto [opcioj]\n"
               "Opcioj:\n"
               " -h       Montru ĉi tiun helpmesaĝon\n"
               " -f       Rulu la ludon en fenestro (defaŭlto)\n"
               " -p       Rulu la ludon plenekrane\n");
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
run_main_loop(struct data *data)
{
        SDL_Event event;
        bool had_event;

        while (!data->quit) {
                if (data->redraw_queued) {
                        had_event = SDL_PollEvent(&event);
                } else {
                        had_event = SDL_WaitEvent(&event);
                }

                if (had_event)
                        handle_event(data, &event);
                else if (data->redraw_queued)
                        handle_redraw(data);
        }
}

int
main(int argc, char **argv)
{
        struct data *data = fv_calloc(sizeof *data);
        int ret = EXIT_SUCCESS;

        data->fullscreen_opt = false;
        data->x_pos = FV_MAP_WIDTH / 2;
        data->y_pos = FV_MAP_HEIGHT / 2;
        data->distance = FV_EDITOR_MIN_DISTANCE;
        data->rotation = 0;

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

        if (!fv_pipeline_data_init(data->vk_data,
                                   data->vk_data->render_pass,
                                   &data->pipeline_data))
                goto out_window;

        if (!create_graphics(data))
                goto out_pipeline_data;

        queue_redraw(data);
        run_main_loop(data);

        destroy_graphics(data);

out_pipeline_data:
        fv_pipeline_data_destroy(data->vk_data,
                                 &data->pipeline_data);
out_window:
        fv_window_free(data->window);
out_data:
        fv_data_deinit();
        fv_free(data);

        return ret;
}
