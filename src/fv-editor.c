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
#include <string.h>
#include <errno.h>
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
#include "fv-error-message.h"
#include "fv-buffer.h"

#define FV_EDITOR_FRUSTUM_TOP 1.428f
/* 40° vertical FOV angle when the height of the display is
 * FV_EDITOR_FRUSTUM_TOP*2
 * ie, top / tan(40 / 2)
 */
#define FV_EDITOR_NEAR_PLANE 3.9233977549812007f
#define FV_EDITOR_FAR_PLANE 57.143f

#define FV_EDITOR_MIN_DISTANCE 14.286f
#define FV_EDITOR_MAX_DISTANCE 42.857f

#define FV_EDITOR_N_SPECIALS (FV_N_ELEMENTS(special_map))

struct color_map {
        int r, g, b, value;
};

static const struct color_map
top_map[] = {
        { 0xbb, 0x99, 0x55, 4 }, /* brick flooring */
        { 0xcc, 0x99, 0x00, 0 }, /* wall top */
        { 0x44, 0x55, 0x22, 6 }, /* grass */
        { 0xee, 0xee, 0xee, 2 }, /* bathroom floor */
        { 0x55, 0x22, 0x22, 19 }, /* room floor */
        { 0x99, 0x33, 0x33, 21 }, /* wood */
        { 0x55, 0x44, 0xcc, 31 }, /* sleeping bag 1 */
        { 0x55, 0x44, 0xdd, 32 }, /* sleeping bag 2 */
        { -1 }
};

static const struct color_map
side_map[] = {
        { 0x66, 0x44, 0x44, 8 }, /* brick wall */
        { 0x99, 0xcc, 0xcc, 11 }, /* inner wall */
        { 0xdd, 0xdd, 0xdd, 14 }, /* bathroom wall */
        { 0xcc, 0xcc, 0xcc, 16 }, /* bathroom wall special */
        { 0x99, 0x11, 0x11, 23 }, /* table side */
        { 0x55, 0x66, 0xcc, 25 }, /* welcome poster 1 */
        { 0x55, 0x66, 0xdd, 28 }, /* welcome poster 2 */
        { 0x00, 0x00, 0x11, 34 }, /* chalkboard 1 */
        { 0x00, 0x00, 0x22, 37 }, /* chalkboard 2 */
        { -1 }
};

static const struct color_map
special_map[] = {
        { 0xdd, 0x55, 0x33 }, /* table */
        { 0x22, 0x55, 0x99 }, /* toilet */
        { 0x11, 0xdd, 0xff }, /* teaset */
        { 0x00, 0x00, 0xee }, /* chair */
        { 0xdd, 0x55, 0x55 }, /* bed */
        { 0xbb, 0x33, 0xbb }, /* barrel */
};

struct data {
        struct fv_window *window;
        struct fv_vk_data *vk_data;

        struct fv_pipeline_data pipeline_data;

        struct fv_map_painter *map_painter;
        struct fv_highlight_painter *highlight_painter;

        struct fv_map map;
        struct fv_buffer special_buffer[FV_MAP_TILES_X * FV_MAP_TILES_Y];

        bool quit;
        bool redraw_queued;

        bool fullscreen_opt;

        int x_pos;
        int y_pos;
        int distance;
        int rotation;

        struct {
                fv_map_block_t block;
                bool has_special;
                struct fv_map_special special;
        } clipboard;

        struct fv_buffer highlight_buffer;
};

static void
queue_redraw(struct data *data)
{
        data->redraw_queued = true;
}

static void
redraw_map(struct data *data)
{
        fv_map_painter_map_changed(data->map_painter);
        queue_redraw(data);
}

static struct fv_map_special *
get_special(struct data *data,
            int x, int y)
{
        int tx = x / FV_MAP_TILE_WIDTH;
        int ty = y / FV_MAP_TILE_HEIGHT;
        struct fv_map_tile *tile =
                data->map.tiles + tx + ty * FV_MAP_TILES_X;
        struct fv_map_special *specials =
                (struct fv_map_special *)
                data->special_buffer[tx + ty * FV_MAP_TILES_X].data;
        int i;

        for (i = 0; i < tile->n_specials; i++) {
                if (specials[i].x == x && specials[i].y == y)
                        return specials + i;
        }

        return NULL;
}

static struct fv_map_special *
add_special(struct data *data,
            int x, int y,
            int special_num)
{
        int tx = x / FV_MAP_TILE_WIDTH;
        int ty = y / FV_MAP_TILE_HEIGHT;
        struct fv_map_tile *tile =
                data->map.tiles + tx + ty * FV_MAP_TILES_X;
        struct fv_buffer *special_buffer =
                data->special_buffer + tx + ty * FV_MAP_TILES_X;
        struct fv_map_special *special;

        special = get_special(data, x, y);
        if (special != NULL)
                return NULL;

        fv_buffer_set_length(special_buffer,
                             (tile->n_specials + 1) *
                             sizeof (struct fv_map_special));

        tile->specials = (struct fv_map_special *) special_buffer->data;

        special = ((struct fv_map_special *) special_buffer->data +
                   tile->n_specials);

        special->num = special_num;
        special->x = x;
        special->y = y;
        special->rotation = 0;

        tile->n_specials++;

        return special;
}

static void
add_special_at_cursor(struct data *data)
{
        add_special(data, data->x_pos, data->y_pos, 0);
        redraw_map(data);
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

static void
toggle_height(struct data *data)
{
        fv_map_block_t *block = (data->map.blocks +
                                 data->x_pos +
                                 data->y_pos * FV_MAP_WIDTH);
        int new_type;

        switch (FV_MAP_GET_BLOCK_TYPE(*block)) {
        case FV_MAP_BLOCK_TYPE_FLOOR:
                new_type = FV_MAP_BLOCK_TYPE_HALF_WALL;
                break;
        case FV_MAP_BLOCK_TYPE_HALF_WALL:
                new_type = FV_MAP_BLOCK_TYPE_FULL_WALL;
                break;
        case FV_MAP_BLOCK_TYPE_FULL_WALL:
                new_type = FV_MAP_BLOCK_TYPE_SPECIAL;
                break;
        case FV_MAP_BLOCK_TYPE_SPECIAL:
                new_type = FV_MAP_BLOCK_TYPE_FLOOR;
                break;
        default:
                /* Don't modify special blocks */
                return;
        }

        *block = (*block & ~FV_MAP_BLOCK_TYPE_MASK) | new_type;

        redraw_map(data);
}

static const struct color_map *
lookup_color(const struct color_map *map,
             int value)
{
        int i;

        for (i = 0; map[i].r != -1; i++) {
                if (map[i].value == value)
                        return map + i;
        }

        return map;
}

static void
next_image(struct data *data,
           int image_offset,
           const struct color_map *map)
{
        fv_map_block_t *block =
                data->map.blocks + data->x_pos + data->y_pos * FV_MAP_WIDTH;
        int value = (*block >> (image_offset * 6)) & ((1 << 6) - 1);
        const struct color_map *color;

        color = lookup_color(map, value) + 1;
        if (color->r == -1)
                color = map;

        *block = ((*block & ~(((1 << 6) - 1) << (image_offset * 6))) |
                  (color->value << (image_offset * 6)));

        redraw_map(data);
}

static void
next_top(struct data *data)
{
        next_image(data, 0, top_map);
}

static void
next_side(struct data *data,
          int side_num)
{
        side_num = (side_num + data->rotation) % 4;
        next_image(data, side_num + 1, side_map);
}

static void
next_special(struct data *data)
{
        struct fv_map_special *special =
                get_special(data,
                            data->x_pos, data->y_pos);

        if (special == NULL)
                return;

        special->num = (special->num + 1) % FV_EDITOR_N_SPECIALS;

        redraw_map(data);
}

static void
remove_special(struct data *data,
               int x, int y)
{
        struct fv_map_special *special = get_special(data, x, y);
        struct fv_map_tile *tile;
        int tx, ty;

        if (special == NULL)
                return;

        tx = x / FV_MAP_TILE_WIDTH;
        ty = y / FV_MAP_TILE_HEIGHT;
        tile = data->map.tiles + tx + ty * FV_MAP_TILES_X;

        /* Replace this special with the last one */
        *special = tile->specials[--tile->n_specials];
}

static void
remove_special_at_cursor(struct data *data)
{
        remove_special(data, data->x_pos, data->y_pos);
        redraw_map(data);
}

static void
rotate_special(struct data *data,
               int amount)
{
        struct fv_map_special *special =
                get_special(data, data->x_pos, data->y_pos);

        if (special == NULL)
                return;

        special->rotation += amount;

        redraw_map(data);
}

static void
copy(struct data *data)
{
        struct fv_map_special *special;

        data->clipboard.block =
                data->map.blocks[data->x_pos + data->y_pos * FV_MAP_WIDTH];

        special = get_special(data, data->x_pos, data->y_pos);
        data->clipboard.has_special = special != NULL;

        if (special)
                data->clipboard.special = *special;
}

static void
paste(struct data *data)
{
        struct fv_map_special *new_special, *old_special;

        data->map.blocks[data->x_pos + data->y_pos * FV_MAP_WIDTH] =
                data->clipboard.block;

        /* Remove any current special */
        remove_special(data, data->x_pos, data->y_pos);

        if (data->clipboard.has_special) {
                old_special = &data->clipboard.special;
                new_special = add_special(data,
                                          data->x_pos, data->y_pos,
                                          old_special->num);
                if (new_special != NULL)
                        new_special->rotation = old_special->rotation;
        }

        redraw_map(data);
}

static void
set_pixel(uint8_t *buf,
          int x, int y,
          int ox, int oy,
          const struct color_map *color)
{
        y = FV_MAP_HEIGHT - 1 - y;
        buf += (x * 4 + ox) * 3 + (y * 4 + oy) * FV_MAP_WIDTH * 4 * 3;
        buf[0] = color->r;
        buf[1] = color->g;
        buf[2] = color->b;
}

static void
set_corners(uint8_t *buf,
            int x, int y,
            const struct color_map *color)
{
        set_pixel(buf, x, y, 0, 0, color);
        set_pixel(buf, x, y, 3, 0, color);
        set_pixel(buf, x, y, 0, 3, color);
        set_pixel(buf, x, y, 3, 3, color);
}

static void
save_block(uint8_t *buf,
           int x, int y,
           fv_map_block_t block)
{
        const struct color_map *color;
        int type;
        int i, j;

        color = lookup_color(top_map, FV_MAP_GET_BLOCK_TOP_IMAGE(block));

        for (i = 0; i < 4; i++) {
                for (j = 0; j < 4; j++) {
                        set_pixel(buf, x, y, i, j, color);
                }
        }

        type = FV_MAP_GET_BLOCK_TYPE(block);

        if (type == FV_MAP_BLOCK_TYPE_SPECIAL) {
                set_pixel(buf, x, y, 1, 2, side_map);
                set_corners(buf, x, y, side_map);
        } else if (type != FV_MAP_BLOCK_TYPE_FLOOR) {
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_NORTH_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, i, 0, color);
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_EAST_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, 3, i, color);
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_SOUTH_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, i + 1, 3, color);
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_WEST_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, 0, i + 1, color);

                if (type == FV_MAP_BLOCK_TYPE_HALF_WALL)
                        set_pixel(buf, x, y, 1, 2, color);
        }
}

static void
save_special(struct data *data,
             uint8_t *buf,
             uint8_t *p,
             const struct fv_map_special *special)
{
        set_corners(buf, special->x, special->y, side_map + 1);

        p[0] = special->x;
        p[1] = special->y;
        p[2] = special->num;
        p[4] = special->rotation >> 8;
        p[5] = special->rotation & 0xff;
}

static void
save(struct data *data)
{
        uint8_t *buf, *p;
        char *filename;
        FILE *out;
        int y, x, i, j;
        int specials_start;
        int special_lines;
        int n_specials = 0;
        int img_width, img_height;

        filename = fv_data_get_filename("../fv-map.ppm");

        if (filename == NULL) {
                fv_error_message("error getting save filename");
                return;
        }

        for (i = 0; i < FV_MAP_TILES_X * FV_MAP_TILES_Y; i++)
                n_specials += data->map.tiles[i].n_specials;

        img_width = FV_MAP_WIDTH * 4;

        specials_start = FV_MAP_HEIGHT * 4 + 4;
        special_lines = (n_specials + img_width / 2 - 1) / (img_width / 2);

        img_height = specials_start + special_lines;

        buf = fv_alloc(img_width * img_height * 3);

        /* Initialise the specials area to white */
        memset(buf + img_width * 3 * FV_MAP_HEIGHT * 4,
               0xff,
               (specials_start - FV_MAP_HEIGHT * 4 + special_lines) *
               img_width * 3);

        for (y = 0; y < FV_MAP_HEIGHT; y++) {
                for (x = 0; x < FV_MAP_WIDTH; x++) {
                        save_block(buf,
                                   x, y,
                                   data->map.blocks[y * FV_MAP_WIDTH + x]);
                }
        }

        p = buf + specials_start * img_width * 3;

        for (i = 0; i < FV_MAP_TILES_X * FV_MAP_TILES_Y; i++) {
                for (j = 0; j < data->map.tiles[i].n_specials; j++) {
                        save_special(data,
                                     buf, p,
                                     data->map.tiles[i].specials + j);
                        p += 3 * 2;
                }
        }

        out = fopen(filename, "wb");

        fv_free(filename);

        if (out == NULL) {
                fv_error_message("error saving: %s", strerror(errno));
        } else {
                fprintf(out,
                        "P6\n"
                        "%i %i\n"
                        "255\n",
                        img_width,
                        img_height);

                fwrite(buf, img_width * img_height * 3, 1, out);

                fclose(out);
        }

        fv_free(buf);
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

        case SDLK_h:
                toggle_height(data);
                break;

        case SDLK_s:
                save(data);
                break;

        case SDLK_t:
                next_top(data);
                break;

        case SDLK_i:
                next_side(data, 0);
                break;

        case SDLK_l:
                next_side(data, 1);
                break;

        case SDLK_k:
                next_side(data, 2);
                break;

        case SDLK_j:
                next_side(data, 3);
                break;

        case SDLK_n:
                remove_special_at_cursor(data);
                break;

        case SDLK_m:
                next_special(data);
                break;

        case SDLK_b:
                add_special_at_cursor(data);
                break;

        case SDLK_c:
                copy(data);
                break;

        case SDLK_v:
                paste(data);
                break;

        case SDLK_LEFTPAREN:
        case SDLK_LEFTBRACKET:
                rotate_special(data, 256);
                break;

        case SDLK_RIGHTPAREN:
        case SDLK_RIGHTBRACKET:
                rotate_special(data, -256);
                break;
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

        data->map_painter = fv_map_painter_new(&data->map,
                                               data->vk_data,
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

static float
highlight_z_pos(struct data *data,
                int x, int y)
{
        int block_pos = x + y * FV_MAP_WIDTH;

        switch (FV_MAP_GET_BLOCK_TYPE(data->map.blocks[block_pos])) {
        case FV_MAP_BLOCK_TYPE_FULL_WALL:
                return 2.1f;
        case FV_MAP_BLOCK_TYPE_HALF_WALL:
                return 1.1f;
        default:
                return 0.1f;
        }

}

static struct fv_highlight_painter_highlight *
next_highlight(struct data *data)
{
        size_t old_offset = data->highlight_buffer.length;

        fv_buffer_set_length(&data->highlight_buffer,
                             old_offset +
                             sizeof (struct fv_highlight_painter_highlight));

        return ((struct fv_highlight_painter_highlight *)
                (data->highlight_buffer.data + old_offset));
}

static void
draw_highlights(struct data *data,
                struct fv_paint_state *paint_state)
{
        struct fv_highlight_painter_highlight *highlight;
        fv_map_block_t block;
        int y, x;

        data->highlight_buffer.length = 0;

        highlight = next_highlight(data);

        highlight->x = data->x_pos;
        highlight->y = data->y_pos;
        highlight->z = highlight_z_pos(data, data->x_pos, data->y_pos);
        highlight->w = 1.0f;
        highlight->h = 1.0f;
        highlight->r = 0.75f * 0.8f * 255.0f;
        highlight->g = 0.75f * 0.8f * 255.0f;
        highlight->b = 1.00f * 0.8f * 255.0f;
        highlight->a = 0.8f * 255.0;

        for (y = 0; y < FV_MAP_HEIGHT; y++) {
                for (x = 0; x < FV_MAP_WIDTH; x++) {
                        block = data->map.blocks[x + y * FV_MAP_WIDTH];
                        if (FV_MAP_GET_BLOCK_TYPE(block) !=
                            FV_MAP_BLOCK_TYPE_SPECIAL)
                                continue;

                        highlight = next_highlight(data);
                        highlight->x = x;
                        highlight->y = y;
                        highlight->z = highlight_z_pos(data, x, y);
                        highlight->w = 1.0f;
                        highlight->h = 1.0f;
                        highlight->r = 0.75f * 0.8f * 255.0f;
                        highlight->g = 1.00f * 0.8f * 255.0f;
                        highlight->b = 0.75f * 0.8f * 255.0f;
                        highlight->a = 0.8f * 255.0;
                }
        }

        for (x = 0; x <= FV_MAP_TILES_X; x++) {
                highlight = next_highlight(data);
                highlight->x = x * FV_MAP_TILE_WIDTH - 0.025;;
                highlight->y = 0.0f;
                highlight->z = 0.1f;
                highlight->w = 0.05f;
                highlight->h = FV_MAP_HEIGHT;
                highlight->r = 1.00f * 0.8f * 255.0f;
                highlight->g = 0.00f * 0.8f * 255.0f;
                highlight->b = 0.00f * 0.8f * 255.0f;
                highlight->a = 0.8f * 255.0;
        }

        for (y = 0; y <= FV_MAP_TILES_Y; y++) {
                highlight = next_highlight(data);
                highlight->x = 0.0f;
                highlight->y = y * FV_MAP_TILE_HEIGHT - 0.025;
                highlight->z = 0.1f;
                highlight->w = FV_MAP_WIDTH;
                highlight->h = 0.05f;
                highlight->r = 1.00f * 0.8f * 255.0f;
                highlight->g = 0.00f * 0.8f * 255.0f;
                highlight->b = 0.00f * 0.8f * 255.0f;
                highlight->a = 0.8f * 255.0;
        }

        highlight = ((struct fv_highlight_painter_highlight *)
                     data->highlight_buffer.data);

        fv_highlight_painter_paint(data->highlight_painter,
                                   data->vk_data->command_buffer,
                                   data->highlight_buffer.length /
                                   sizeof *highlight,
                                   highlight,
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
        int i;

        data->fullscreen_opt = false;
        data->x_pos = FV_MAP_WIDTH / 2;
        data->y_pos = FV_MAP_HEIGHT / 2;
        data->distance = FV_EDITOR_MIN_DISTANCE;
        data->rotation = 0;
        data->map = fv_map;

        fv_buffer_init(&data->highlight_buffer);

        for (i = 0; i < FV_MAP_TILES_X * FV_MAP_TILES_Y; i++) {
                fv_buffer_init(data->special_buffer + i);
                fv_buffer_append(data->special_buffer + i,
                                 fv_map.tiles[i].specials,
                                 fv_map.tiles[i].n_specials *
                                 sizeof (struct fv_map_special));
                data->map.tiles[i].specials =
                        (const struct fv_map_special *)
                        data->special_buffer[i].data;
        }

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
        for (i = 0; i < FV_MAP_TILES_X * FV_MAP_TILES_Y; i++)
                fv_buffer_destroy(data->special_buffer + i);
        fv_buffer_destroy(&data->highlight_buffer);
        fv_free(data);

        return ret;
}
