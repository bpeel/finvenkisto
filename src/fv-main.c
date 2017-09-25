/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014, 2015, 2016 Neil Roberts
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
#include <time.h>
#include <stdarg.h>
#include <GL/glx.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-image-data-old.h"
#include "fv-shader-data.h"
#include "fv-gl.h"
#include "fv-vk.h"
#include "fv-util.h"
#include "fv-hud.h"
#include "fv-buffer.h"
#include "fv-map.h"
#include "fv-error-message.h"
#include "fv-data.h"
#include "fv-pipeline-data.h"
#include "fv-vk-data.h"

#define CORE_GL_MAJOR_VERSION 3
#define CORE_GL_MINOR_VERSION 3

#define COLOR_IMAGE_FORMAT VK_FORMAT_B8G8R8A8_SRGB

enum key_code {
        KEY_CODE_UP,
        KEY_CODE_DOWN,
        KEY_CODE_LEFT,
        KEY_CODE_RIGHT,
        KEY_CODE_SHOUT
};

#define N_KEYS 5

enum key_type {
        KEY_TYPE_KEYBOARD,
        KEY_TYPE_MOUSE
};

struct key {
        enum key_type type;
        unsigned int keysym;
        unsigned int button;
        bool down;
};

struct player {
        struct key keys[N_KEYS];

        int viewport_x, viewport_y;
        int viewport_width, viewport_height;
        float center_x, center_y;
};

enum menu_state {
        MENU_STATE_CHOOSING_N_PLAYERS,
        MENU_STATE_CHOOSING_KEYS,
        MENU_STATE_PLAYING
};

struct data {
        struct fv_image_data_old *image_data;

        Display *display;
        Window x_window;
        GLXWindow glx_window;
        GLXContext glx_context;

        /* Permanant vulkan resources */
        struct fv_vk_data vk_data;
        VkInstance vk_instance;
        VkFormat vk_depth_format;
        VkQueue vk_queue;
        VkCommandPool vk_command_pool;
        VkRenderPass vk_render_pass;
        VkFence vk_fence;

        /* Resources that are recreated lazily whenever the
         * framebuffer size changes.
         */
        struct {
                VkImage color_image;
                VkImage linear_image;
                VkImage depth_image;
                VkDeviceMemory memory;
                VkDeviceMemory linear_memory;
                bool need_linear_memory_invalidate;
                void *linear_memory_map;
                VkDeviceSize linear_memory_stride;
                VkImageView color_image_view;
                VkImageView depth_image_view;
                VkFramebuffer framebuffer;
                int width, height;
        } vk_fb;

        GLuint blit_program;

        bool window_mapped;
        int fb_width, fb_height;
        int last_fb_width, last_fb_height;

        struct {
                struct fv_shader_data shader_data;
                struct fv_game *game;
                struct fv_hud *hud;
                bool shader_data_loaded;
        } graphics;

        struct fv_pipeline_data pipeline_data;

        struct fv_logic *logic;

        bool quit;
        bool is_fullscreen;

        bool viewports_dirty;
        int n_viewports;

        struct timespec start_time;

        enum menu_state menu_state;
        int n_players;
        int next_player;
        enum key_code next_key;

        struct player players[FV_LOGIC_MAX_PLAYERS];
};

static void
reset_start_time(struct data *data)
{
        clock_gettime(CLOCK_MONOTONIC, &data->start_time);
}

static unsigned int
get_ticks(struct data *data)
{
        struct timespec now;

        clock_gettime(CLOCK_MONOTONIC, &now);

        return ((now.tv_nsec - data->start_time.tv_nsec) / 1000000 +
                ((long) now.tv_sec - (long) data->start_time.tv_sec) * 1000);
}

static void
reset_menu_state(struct data *data)
{
        int i, j;

        data->menu_state = MENU_STATE_CHOOSING_N_PLAYERS;
        reset_start_time(data);
        data->viewports_dirty = true;
        data->n_viewports = 1;

        for (i = 0; i < FV_LOGIC_MAX_PLAYERS; i++) {
                for (j = 0; j < N_KEYS; j++) {
                        data->players[i].keys[j].down = false;
                        data->players[i].keys[j].down = false;
                }
        }

        fv_logic_reset(data->logic, 0);
}

static void
handle_configure_event(struct data *data,
                       const XConfigureEvent *event)
{
        data->fb_width = event->width;
        data->fb_height = event->height;
}

static void
toggle_fullscreen(struct data *data)
{
        /* FIXME */
}

static void
update_direction(struct data *data,
                 int player_num)
{
        const struct player *player = data->players + player_num;
        float direction;
        bool moving = true;
        int pressed_keys = 0;
        int key_mask;

        if (player->keys[KEY_CODE_UP].down)
                pressed_keys |= 1 << KEY_CODE_UP;
        if (player->keys[KEY_CODE_DOWN].down)
                pressed_keys |= 1 << KEY_CODE_DOWN;
        if (player->keys[KEY_CODE_LEFT].down)
                pressed_keys |= 1 << KEY_CODE_LEFT;
        if (player->keys[KEY_CODE_RIGHT].down)
                pressed_keys |= 1 << KEY_CODE_RIGHT;

        /* Cancel out directions where opposing keys are pressed */
        key_mask = ((pressed_keys & 10) >> 1) ^ (pressed_keys & 5);
        key_mask |= key_mask << 1;
        pressed_keys &= key_mask;

        switch (pressed_keys) {
        case 1 << KEY_CODE_UP:
                direction = M_PI / 2.0f;
                break;
        case (1 << KEY_CODE_UP) | (1 << KEY_CODE_LEFT):
                direction = M_PI * 3.0f / 4.0f;
                break;
        case (1 << KEY_CODE_UP) | (1 << KEY_CODE_RIGHT):
                direction = M_PI / 4.0f;
                break;
        case 1 << KEY_CODE_DOWN:
                direction = -M_PI / 2.0f;
                break;
        case (1 << KEY_CODE_DOWN) | (1 << KEY_CODE_LEFT):
                direction = -M_PI * 3.0f / 4.0f;
                break;
        case (1 << KEY_CODE_DOWN) | (1 << KEY_CODE_RIGHT):
                direction = -M_PI / 4.0f;
                break;
        case 1 << KEY_CODE_LEFT:
                direction = M_PI;
                break;
        case 1 << KEY_CODE_RIGHT:
                direction = 0.0f;
                break;
        default:
                moving = false;
                direction = 0.0f;
                break;
        }

        fv_logic_set_direction(data->logic, player_num, moving, direction);
}

static bool
is_key(const struct key *key,
       const struct key *other_key)
{
        if (key->type != other_key->type)
                return false;

        switch (key->type) {
        case KEY_TYPE_KEYBOARD:
                return key->keysym == other_key->keysym;

        case KEY_TYPE_MOUSE:
                return key->button == other_key->button;
        }

        assert(false);

        return false;
}

static void
set_key(struct data *data,
        const struct key *other_key)
{
        data->players[data->next_player].keys[data->next_key] = *other_key;
        data->next_key++;

        if (data->next_key >= N_KEYS) {
                data->next_player++;
                data->next_key = 0;

                if (data->next_player >= data->n_players) {
                        data->menu_state = MENU_STATE_PLAYING;
                        reset_start_time(data);
                        fv_logic_reset(data->logic, data->n_players);
                }
        }
}

static void
set_key_state(struct data *data,
              int player_num,
              enum key_code key,
              bool state)
{
        bool old_state = data->players[player_num].keys[key].down;

        if (old_state == state)
                return;

        data->players[player_num].keys[key].down = state;

        if (key == KEY_CODE_SHOUT) {
                if (data->menu_state == MENU_STATE_PLAYING && state)
                        fv_logic_shout(data->logic, player_num);
        } else {
                update_direction(data, player_num);
        }
}

static void
handle_key(struct data *data,
           const struct key *key)
{
        struct key *player_key;
        int i, j;

        switch (data->menu_state) {
        case MENU_STATE_CHOOSING_N_PLAYERS:
                break;

        case MENU_STATE_CHOOSING_KEYS:
                for (i = 0; i < data->next_player; i++) {
                        for (j = 0; j < N_KEYS; j++) {
                                player_key = data->players[i].keys + j;
                                if (is_key(player_key, key)) {
                                        set_key_state(data, i, j, key->down);
                                        goto handled;
                                }
                        }
                }

                for (j = 0; j < data->next_key; j++) {
                        player_key = data->players[i].keys + j;
                        if (is_key(player_key, key)) {
                                set_key_state(data, i, j, key->down);
                                goto handled;
                        }
                }

                if (key->down)
                        set_key(data, key);

        handled:
                break;

        case MENU_STATE_PLAYING:
                for (i = 0; i < data->n_players; i++) {
                        for (j = 0; j < N_KEYS; j++) {
                                player_key = data->players[i].keys + j;
                                if (is_key(player_key, key)) {
                                        set_key_state(data, i, j, key->down);
                                        goto found;
                                }
                        }
                }
        found:
                break;
        }
}

static void
handle_other_key(struct data *data,
                 const XKeyEvent *event,
                 KeySym keysym)
{
        struct key key;

        if (data->menu_state == MENU_STATE_CHOOSING_N_PLAYERS) {
                if (event->type == KeyPress &&
                    keysym >= XK_1 &&
                    keysym < XK_1 + FV_LOGIC_MAX_PLAYERS) {
                        data->n_players = keysym - XK_1 + 1;
                        data->next_player = 0;
                        data->next_key = 0;
                        data->menu_state = MENU_STATE_CHOOSING_KEYS;
                        data->viewports_dirty = true;
                }

                return;
        }

        key.type = KEY_TYPE_KEYBOARD;
        key.keysym = keysym;
        key.down = event->type == KeyPress;

        handle_key(data, &key);
}

static void
handle_key_event(struct data *data,
                 const XKeyEvent *event)
{
        KeySym keysym = XLookupKeysym((XKeyEvent *) event,
                                      0 /* index */);

        switch (keysym) {
        case XK_Escape:
                if (event->type == KeyPress) {
                        if (data->menu_state == MENU_STATE_CHOOSING_N_PLAYERS)
                                data->quit = true;
                        else
                                reset_menu_state(data);
                }
                break;

        case XK_F11:
                if (event->type == KeyPress)
                        toggle_fullscreen(data);
                break;

        default:
                handle_other_key(data, event, keysym);
                break;
        }
}

static void
handle_mouse_button(struct data *data,
                    const XButtonEvent *event)
{
        struct key key;

        key.type = KEY_TYPE_MOUSE;
        key.button = event->button;
        key.down = event->type == ButtonPress;

        handle_key(data, &key);
}

static void
destroy_graphics(struct data *data)
{
        if (data->graphics.game) {
                fv_game_free(data->graphics.game);
                data->graphics.game = NULL;
        }

        if (data->graphics.shader_data_loaded) {
                fv_shader_data_destroy(&data->graphics.shader_data);
                data->graphics.shader_data_loaded = false;
        }

        if (data->graphics.hud) {
                fv_hud_free(data->graphics.hud);
                data->graphics.hud = NULL;
        }
}

static bool
create_graphics(struct data *data)
{
        memset(&data->graphics, 0, sizeof data->graphics);

        /* All of the painting functions expect to have the default
         * OpenGL state plus the following modifications */

        fv_gl.glEnable(GL_CULL_FACE);
        fv_gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        /* The current program, vertex array, array buffer and bound
         * textures are not expected to be reset back to zero */

        data->last_fb_width = data->last_fb_height = 0;

        if (!fv_shader_data_init(&data->graphics.shader_data))
                goto error;

        data->graphics.shader_data_loaded = true;

        data->graphics.hud = fv_hud_new(data->image_data,
                                        &data->graphics.shader_data);

        if (data->graphics.hud == NULL)
                goto error;

        data->graphics.game = fv_game_new(&data->vk_data,
                                          &data->pipeline_data);

        if (data->graphics.game == NULL)
                goto error;

        return true;

error:
        destroy_graphics(data);
        return false;
}

static void
handle_event(struct data *data,
             const XEvent *event)
{
        switch (event->type) {
        case MapNotify:
                data->window_mapped = true;
                goto handled;

        case UnmapNotify:
                data->window_mapped = false;
                goto handled;

        case ConfigureNotify:
                handle_configure_event(data, &event->xconfigure);
                goto handled;

        case KeyPress:
        case KeyRelease:
                handle_key_event(data, &event->xkey);
                goto handled;

        case ButtonPress:
        case ButtonRelease:
                handle_mouse_button(data, &event->xbutton);
                goto handled;
        }

handled:
        (void) 0;
}

static void
paint_hud(struct data *data,
          int w, int h)
{
        switch (data->menu_state) {
        case MENU_STATE_CHOOSING_N_PLAYERS:
                fv_hud_paint_player_select(data->graphics.hud, w, h);
                break;
        case MENU_STATE_CHOOSING_KEYS:
                fv_hud_paint_key_select(data->graphics.hud,
                                        w, h,
                                        data->next_player,
                                        data->next_key,
                                        data->n_players);
                break;
        case MENU_STATE_PLAYING:
                fv_hud_paint_game_state(data->graphics.hud,
                                        w, h,
                                        data->logic);
                break;
        }
}

static void
update_viewports(struct data *data)
{
        int viewport_width, viewport_height;
        int vertical_divisions = 1;
        int i;

        if (!data->viewports_dirty)
                return;

        if (data->menu_state == MENU_STATE_CHOOSING_N_PLAYERS)
                data->n_viewports = 1;
        else
                data->n_viewports = data->n_players;

        viewport_width = data->last_fb_width;
        viewport_height = data->last_fb_height;

        if (data->n_viewports > 1) {
                viewport_width /= 2;
                if (data->n_viewports > 2) {
                        viewport_height /= 2;
                        vertical_divisions = 2;
                }
        }

        for (i = 0; i < data->n_viewports; i++) {
                data->players[i].viewport_x = i % 2 * viewport_width;
                data->players[i].viewport_y = (vertical_divisions - 1 -
                                               i / 2) * viewport_height;
                data->players[i].viewport_width = viewport_width;
                data->players[i].viewport_height = viewport_height;
        }

        data->viewports_dirty = false;
}

static void
update_centers(struct data *data)
{
        int i;

        if (data->menu_state == MENU_STATE_PLAYING) {
                for (i = 0; i < data->n_viewports; i++) {
                        fv_logic_get_center(data->logic,
                                            i,
                                            &data->players[i].center_x,
                                            &data->players[i].center_y);
                }
        } else {
                for (i = 0; i < data->n_viewports; i++) {
                        data->players[i].center_x = FV_MAP_START_X;
                        data->players[i].center_y = FV_MAP_START_Y;
                }
        }
}

static bool
need_clear(struct data *data)
{
        struct player *player;
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
                player = data->players + i;
                if (!fv_game_covers_framebuffer(data->graphics.game,
                                                player->center_x,
                                                player->center_y,
                                                player->viewport_width,
                                                player->viewport_height))
                        return true;
        }

        return false;
}

static VkResult
allocate_image_store(const struct fv_vk_data *vk_data,
                     uint32_t memory_type_flags,
                     int n_images,
                     const VkImage *images,
                     VkDeviceMemory *memory_out,
                     int *memory_type_index_out)
{
        VkDeviceMemory memory;
        VkMemoryRequirements reqs;
        VkResult res;
        int offset = 0;
        int *offsets = alloca(sizeof *offsets * n_images);
        int memory_type_index;
        uint32_t usable_memory_types = UINT32_MAX;
        VkDeviceSize granularity;
        int i;

        granularity = vk_data->device_properties.limits.bufferImageGranularity;

        for (i = 0; i < n_images; i++) {
                fv_vk.vkGetImageMemoryRequirements(vk_data->device,
                                                   images[i],
                                                   &reqs);
                offset = fv_align(offset, granularity);
                offset = fv_align(offset, reqs.alignment);
                offsets[i] = offset;
                offset += reqs.size;

                usable_memory_types &= reqs.memoryTypeBits;
        }

        while (usable_memory_types) {
                i = fv_util_ffs(usable_memory_types) - 1;

                if ((vk_data->memory_properties.memoryTypes[i].propertyFlags &
                     memory_type_flags) == memory_type_flags) {
                        memory_type_index = i;
                        goto found_memory_type;
                }

                usable_memory_types &= ~(1 << i);
        }

        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
found_memory_type: (void) 0;

        VkMemoryAllocateInfo allocate_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = offset,
                .memoryTypeIndex = memory_type_index
        };
        res = fv_vk.vkAllocateMemory(vk_data->device,
                                     &allocate_info,
                                     NULL, /* allocator */
                                     &memory);
        if (res != VK_SUCCESS)
                return res;

        for (i = 0; i < n_images; i++) {
                fv_vk.vkBindImageMemory(vk_data->device,
                                        images[i],
                                        memory,
                                        offsets[i]);
        }

        *memory_out = memory;
        if (memory_type_index_out)
                *memory_type_index_out = memory_type_index;

        return VK_SUCCESS;
}

static void
destroy_framebuffer_resources(struct data *data)
{
        if (data->vk_fb.depth_image_view)
                fv_vk.vkDestroyImageView(data->vk_data.device,
                                         data->vk_fb.depth_image_view,
                                         NULL /* allocator */);
        if (data->vk_fb.color_image_view)
                fv_vk.vkDestroyImageView(data->vk_data.device,
                                         data->vk_fb.color_image_view,
                                         NULL /* allocator */);
        if (data->vk_fb.framebuffer)
                fv_vk.vkDestroyFramebuffer(data->vk_data.device,
                                           data->vk_fb.framebuffer,
                                           NULL /* allocator */);
        if (data->vk_fb.linear_memory_map)
                fv_vk.vkUnmapMemory(data->vk_data.device,
                                    data->vk_fb.linear_memory);
        if (data->vk_fb.linear_memory)
                fv_vk.vkFreeMemory(data->vk_data.device,
                                   data->vk_fb.linear_memory,
                                   NULL /* allocator */);
        if (data->vk_fb.memory)
                fv_vk.vkFreeMemory(data->vk_data.device,
                                   data->vk_fb.memory,
                                   NULL /* allocator */);
        if (data->vk_fb.depth_image)
                fv_vk.vkDestroyImage(data->vk_data.device,
                                     data->vk_fb.depth_image,
                                     NULL /* allocator */);
        if (data->vk_fb.linear_image)
                fv_vk.vkDestroyImage(data->vk_data.device,
                                     data->vk_fb.linear_image,
                                     NULL /* allocator */);
        if (data->vk_fb.color_image)
                fv_vk.vkDestroyImage(data->vk_data.device,
                                     data->vk_fb.color_image,
                                     NULL /* allocator */);

        memset(&data->vk_fb, 0, sizeof data->vk_fb);
}

static bool
create_framebuffer_resources(struct data *data)
{
        VkResult res;
        int linear_memory_type;

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = COLOR_IMAGE_FORMAT,
                .extent = {
                        .width = data->fb_width,
                        .height = data->fb_height,
                        .depth = 1
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        res = fv_vk.vkCreateImage(data->vk_data.device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &data->vk_fb.color_image);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkImage");
                goto error;
        }

        image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_LINEAR;
        res = fv_vk.vkCreateImage(data->vk_data.device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &data->vk_fb.linear_image);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkImage");
                goto error;
        }

        image_create_info.format = data->vk_depth_format;
        image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        res = fv_vk.vkCreateImage(data->vk_data.device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &data->vk_fb.depth_image);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkImage");
                goto error;
        }

        res = allocate_image_store(&data->vk_data,
                                   0, /* memory_type_flags */
                                   2, /* n_images */
                                   (VkImage[]) {
                                           data->vk_fb.color_image,
                                           data->vk_fb.depth_image
                                   },
                                   &data->vk_fb.memory,
                                   NULL /* memory_type_index */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating framebuffer memory");
                goto error;
        }

        res = allocate_image_store(&data->vk_data,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                   1, /* n_images */
                                   &data->vk_fb.linear_image,
                                   &data->vk_fb.linear_memory,
                                   &linear_memory_type);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating framebuffer memory");
                goto error;
        }

        data->vk_fb.need_linear_memory_invalidate =
                (data->vk_data.memory_properties.
                 memoryTypes[linear_memory_type].propertyFlags &
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0;

        VkImageSubresource linear_subresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .arrayLayer = 0
        };
        VkSubresourceLayout linear_layout;
        fv_vk.vkGetImageSubresourceLayout(data->vk_data.device,
                                          data->vk_fb.linear_image,
                                          &linear_subresource,
                                          &linear_layout);
        data->vk_fb.linear_memory_stride = linear_layout.rowPitch;

        res = fv_vk.vkMapMemory(data->vk_data.device,
                                data->vk_fb.linear_memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                &data->vk_fb.linear_memory_map);
        if (res != VK_SUCCESS) {
                fv_error_message("Error mapping linear memory");
                goto error;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = data->vk_fb.color_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = COLOR_IMAGE_FORMAT,
                .components = {
                        .r = VK_COMPONENT_SWIZZLE_R,
                        .g = VK_COMPONENT_SWIZZLE_G,
                        .b = VK_COMPONENT_SWIZZLE_B,
                        .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = fv_vk.vkCreateImageView(data->vk_data.device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &data->vk_fb.color_image_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating image view");
                goto error;
        }

        image_view_create_info.image = data->vk_fb.depth_image;
        image_view_create_info.format = data->vk_depth_format;
        image_view_create_info.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_DEPTH_BIT;
        res = fv_vk.vkCreateImageView(data->vk_data.device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &data->vk_fb.depth_image_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating image view");
                goto error;
        }

        VkImageView attachments[] = {
                data->vk_fb.color_image_view,
                data->vk_fb.depth_image_view
        };
        VkFramebufferCreateInfo framebuffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = data->vk_render_pass,
                .attachmentCount = FV_N_ELEMENTS(attachments),
                .pAttachments = attachments,
                .width = data->fb_width,
                .height = data->fb_height,
                .layers = 1
        };
        res = fv_vk.vkCreateFramebuffer(data->vk_data.device,
                                        &framebuffer_create_info,
                                        NULL, /* allocator */
                                        &data->vk_fb.framebuffer);

        data->vk_fb.width = data->fb_width;
        data->vk_fb.height = data->fb_height;

        return true;

error:
        destroy_framebuffer_resources(data);

        return false;
}

static void
paint_vk(struct data *data)
{
        VkCommandBuffer command_buffer;
        VkResult res;
        int i;

        if (data->vk_fb.width != data->fb_width ||
            data->vk_fb.height != data->fb_height) {
                destroy_framebuffer_resources(data);
                if (!create_framebuffer_resources(data)) {
                        data->quit = true;
                        return;
                }
        }

        VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = data->vk_command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
        };
        res = fv_vk.vkAllocateCommandBuffers(data->vk_data.device,
                                             &command_buffer_allocate_info,
                                             &command_buffer);

        if (res != VK_SUCCESS)
                return;

        VkCommandBufferBeginInfo begin_command_buffer_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        res = fv_vk.vkBeginCommandBuffer(command_buffer,
                                         &begin_command_buffer_info);
        if (res != VK_SUCCESS)
                goto error_command_buffer;

        VkClearValue clear_values[] = {
                [1] = {
                        .depthStencil = {
                                .depth = 1.0f,
                                .stencil = 0
                        }
                }
        };
        VkRenderPassBeginInfo render_pass_begin_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = data->vk_render_pass,
                .framebuffer = data->vk_fb.framebuffer,
                .renderArea = {
                        .offset = { 0, 0 },
                        .extent = { data->fb_width, data->fb_height}
                },
                .clearValueCount = FV_N_ELEMENTS(clear_values),
                .pClearValues = clear_values
        };
        fv_vk.vkCmdBeginRenderPass(command_buffer,
                                   &render_pass_begin_info,
                                   VK_SUBPASS_CONTENTS_INLINE);

        if (need_clear(data)) {
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
                                .extent = { data->fb_width, data->fb_height}
                        },
                        .baseArrayLayer = 0,
                        .layerCount = 1
                };
                fv_vk.vkCmdClearAttachments(command_buffer,
                                            1, /* attachmentCount */
                                            &color_clear_attachment,
                                            1,
                                            &color_clear_rect);
        }

        VkRect2D scissor = {
                .offset = { .x = 0, .y = 0 },
                .extent = { .width = data->fb_width, .height = data->fb_height }
        };
        fv_vk.vkCmdSetScissor(command_buffer,
                              0, /* firstScissor */
                              1, /* scissorCount */
                              &scissor);

        for (i = 0; i < data->n_viewports; i++) {
                VkViewport viewport = {
                        .x = data->players[i].viewport_x,
                        .y = data->players[i].viewport_y,
                        .width = data->players[i].viewport_width,
                        .height = data->players[i].viewport_height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                };
                fv_vk.vkCmdSetViewport(command_buffer,
                                       0, /* firstViewport */
                                       1, /* viewportCount */
                                       &viewport);
                fv_game_paint(data->graphics.game,
                              data->players[i].center_x,
                              data->players[i].center_y,
                              data->players[i].viewport_width,
                              data->players[i].viewport_height,
                              data->logic,
                              command_buffer);
        }

        fv_vk.vkCmdEndRenderPass(command_buffer);

        VkImageCopy copy_region = {
                .srcSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                },
                .srcOffset = { 0, 0, 0 },
                .dstSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                },
                .dstOffset = { 0, 0, 0 },
                .extent = { data->fb_width, data->fb_height, 1 }
        };
        fv_vk.vkCmdCopyImage(command_buffer,
                             data->vk_fb.color_image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             data->vk_fb.linear_image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             1, /* regionCount */
                             &copy_region);

        res = fv_vk.vkEndCommandBuffer(command_buffer);
        if (res != VK_SUCCESS)
                goto error_command_buffer;

        fv_vk.vkResetFences(data->vk_data.device,
                            1, /* fenceCount */
                            &data->vk_fence);

        VkSubmitInfo submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &command_buffer,
        };
        res = fv_vk.vkQueueSubmit(data->vk_queue,
                                  1, /* submitCount */
                                  &submit_info,
                                  data->vk_fence);
        if (res != VK_SUCCESS)
                goto error_command_buffer;

        res = fv_vk.vkWaitForFences(data->vk_data.device,
                                    1, /* fenceCount */
                                    &data->vk_fence,
                                    VK_TRUE, /* waitAll */
                                    UINT64_MAX);
        if (res != VK_SUCCESS)
                goto error_command_buffer;

        if (data->vk_fb.need_linear_memory_invalidate) {
                VkMappedMemoryRange memory_range = {
                        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                        .memory = data->vk_fb.linear_memory,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                };
                fv_vk.vkInvalidateMappedMemoryRanges(data->vk_data.device,
                                                     1, /* memoryRangeCount */
                                                     &memory_range);
        }

error_command_buffer:
        fv_vk.vkFreeCommandBuffers(data->vk_data.device,
                                   data->vk_command_pool,
                                   1, /* commandBufferCount */
                                   &command_buffer);
}

static GLuint
create_blit_program(void)
{
        static const char *vertex_source =
                "attribute vec3 position;\n"
                "attribute vec2 tex_coord_attrib;\n"
                "varying vec2 tex_coord;\n"
                "\n"
                "void\n"
                "main()\n"
                "{\n"
                "        gl_Position = vec4(position, 1.0);\n"
                "        tex_coord = tex_coord_attrib;\n"
                "}\n";
        static const char *frag_source =
                "varying vec2 tex_coord;\n"
                "uniform sampler2D tex;\n"
                "\n"
                "void\n"
                "main()\n"
                "{\n"
                "        gl_FragColor = texture2D(tex, tex_coord);\n"
                "}\n";
        static const GLint length = -1;
        GLuint prog, shader;

        prog = fv_gl.glCreateProgram();

        shader = fv_gl.glCreateShader(GL_VERTEX_SHADER);
        fv_gl.glShaderSource(shader,
                             1, /* count */
                             (const GLchar **) &vertex_source,
                             &length);
        fv_gl.glCompileShader(shader);
        fv_gl.glAttachShader(prog, shader);
        fv_gl.glDeleteShader(shader);

        shader = fv_gl.glCreateShader(GL_FRAGMENT_SHADER);
        fv_gl.glShaderSource(shader,
                             1, /* count */
                             (const GLchar **) &frag_source,
                             &length);
        fv_gl.glCompileShader(shader);
        fv_gl.glAttachShader(prog, shader);
        fv_gl.glDeleteShader(shader);

        fv_gl.glLinkProgram(prog);

        return prog;
}

static void
upload_vk_image(struct data *data)
{
        GLuint tex, vao, vbo;
        int alignment = 1;
        struct vertex {
                float x, y;
                float s, t;
        };
        static const struct vertex verts[] = {
                { -1, -1, 0, 1 },
                { 1, -1, 1, 1 },
                { -1, 1, 0, 0 },
                { 1, 1, 1, 0 },
        };

        while ((data->vk_fb.linear_memory_stride & alignment) == 0 &&
               alignment < 8)
                alignment <<= 1;

        fv_gl.glGenTextures(1, &tex);
        fv_gl.glBindTexture(GL_TEXTURE_2D, tex);
        fv_gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,
                            data->vk_fb.linear_memory_stride / 4);
        fv_gl.glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        fv_gl.glTexImage2D(GL_TEXTURE_2D,
                           0, /* level */
                           GL_RGBA,
                           data->fb_width,
                           data->fb_height,
                           0, /* border */
                           GL_BGRA,
                           GL_UNSIGNED_BYTE,
                           data->vk_fb.linear_memory_map);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MAG_FILTER,
                              GL_NEAREST);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MIN_FILTER,
                              GL_NEAREST);

        fv_gl.glUseProgram(data->blit_program);

        fv_gl.glGenBuffers(1, &vbo);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, vbo);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           sizeof verts,
                           verts,
                           GL_STATIC_DRAW);

        fv_gl.glGenVertexArrays(1, &vao);
        fv_gl.glBindVertexArray(vao);
        fv_gl.glVertexAttribPointer(FV_SHADER_DATA_ATTRIB_POSITION,
                                    2, /* size */
                                    GL_FLOAT,
                                    GL_FALSE, /* normalised */
                                    sizeof verts[0],
                                    (void *) (GLintptr)
                                    offsetof(struct vertex, x));
        fv_gl.glEnableVertexAttribArray(FV_SHADER_DATA_ATTRIB_POSITION);
        fv_gl.glVertexAttribPointer(FV_SHADER_DATA_ATTRIB_TEX_COORD,
                                    2, /* size */
                                    GL_FLOAT,
                                    GL_FALSE, /* normalised */
                                    sizeof verts[0],
                                    (void *) (GLintptr)
                                    offsetof(struct vertex, s));
        fv_gl.glEnableVertexAttribArray(FV_SHADER_DATA_ATTRIB_TEX_COORD);

        fv_gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        fv_gl.glDeleteVertexArrays(1, &vao);
        fv_gl.glDeleteBuffers(1, &vbo);
        fv_gl.glDeleteTextures(1, &tex);
}

static void
paint(struct data *data)
{
        GLbitfield clear_mask = GL_DEPTH_BUFFER_BIT;

        if (data->fb_width != data->last_fb_width ||
            data->fb_height != data->last_fb_height) {
                fv_gl.glViewport(0, 0, data->fb_width, data->fb_height);
                data->last_fb_width = data->fb_width;
                data->last_fb_height = data->fb_height;
                data->viewports_dirty = true;
        }

        fv_logic_update(data->logic, get_ticks(data));

        update_viewports(data);
        update_centers(data);

        if (need_clear(data))
                clear_mask |= GL_COLOR_BUFFER_BIT;

        fv_gl.glClear(clear_mask);

        if (data->n_viewports != 1)
                fv_gl.glViewport(0, 0, data->fb_width, data->fb_height);

        paint_hud(data, data->fb_width, data->fb_height);

        paint_vk(data);

        upload_vk_image(data);

        fv_gl.glXSwapBuffers(data->display, data->glx_window);
}

static bool
check_gl_version(void)
{
        if (fv_gl.major_version < 0 ||
            fv_gl.minor_version < 0) {
                fv_error_message("Invalid GL version string encountered: %s",
                                 (const char *) fv_gl.glGetString(GL_VERSION));

                return false;
        }

        if (fv_gl.major_version < CORE_GL_MAJOR_VERSION ||
            (fv_gl.major_version == CORE_GL_MAJOR_VERSION &&
             fv_gl.minor_version < CORE_GL_MINOR_VERSION)) {
                fv_error_message("GL version %i.%i is required but the driver "
                                 "is reporting:\n"
                                 "Version: %s\n"
                                 "Vendor: %s\n"
                                 "Renderer: %s",
                                 CORE_GL_MAJOR_VERSION,
                                 CORE_GL_MINOR_VERSION,
                                 (const char *) fv_gl.glGetString(GL_VERSION),
                                 (const char *) fv_gl.glGetString(GL_VENDOR),
                                 (const char *) fv_gl.glGetString(GL_RENDERER));
                return false;
        }

        if (fv_gl.glGenerateMipmap == NULL) {
                fv_error_message("glGenerateMipmap is required (from "
                                 "GL_ARB_framebuffer_object)\n"
                                 "Version: %s\n"
                                 "Vendor: %s\n"
                                 "Renderer: %s",
                                 (const char *) fv_gl.glGetString(GL_VERSION),
                                 (const char *) fv_gl.glGetString(GL_VENDOR),
                                 (const char *) fv_gl.glGetString(GL_RENDERER));
                return false;
        }

        return true;
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
                        data->is_fullscreen = false;
                        break;

                case 'p':
                        data->is_fullscreen = true;
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
        XEvent event;

        if (!data->window_mapped) {
                XNextEvent(data->display, &event);
                reset_start_time(data);
                handle_event(data, &event);
                return;
        }

        if (XPending(data->display)) {
                XNextEvent(data->display, &event);
                handle_event(data, &event);
                return;
        }

        paint(data);
}

static GLXFBConfig
choose_fb_config(struct data *data)
{
        GLXFBConfig *configs;
        int n_configs;
        GLXFBConfig ret;
        static const int attrib_list[] = {
                GLX_DOUBLEBUFFER, True,
                GLX_DEPTH_SIZE, 16,
                0
        };

        configs = fv_gl.glXChooseFBConfig(data->display,
                                          DefaultScreen (data->display),
                                          attrib_list,
                                          &n_configs);

        if (configs == NULL)
                return NULL;

        if (n_configs < 1)
                ret = NULL;
        else
                ret = configs[0];

        XFree (configs);

        return ret;
}

static GLXContext
create_context(struct data *data, GLXFBConfig fb_config)
{
        GLXContext context;
        static const int attrib_list[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, CORE_GL_MAJOR_VERSION,
                GLX_CONTEXT_MINOR_VERSION_ARB, CORE_GL_MINOR_VERSION,
                GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
                None
        };

        context = fv_gl.glXCreateContextAttribs(data->display,
                                                fb_config,
                                                NULL, /* shareList */
                                                True, /* direct */
                                                attrib_list);

        if (context == NULL) {
                fv_error_message("Failed to create GLX context");
                return NULL;
        }

        return context;
}

static bool
make_window(struct data *data)
{
        XSetWindowAttributes attr;
        unsigned long mask;
        Window root;
        GLXFBConfig fb_config;
        XVisualInfo *visinfo;

        root = RootWindow(data->display, 0 /* screen */);

        fb_config = choose_fb_config(data);

        if (fb_config == NULL) {
                fv_error_message("Couldn't get an RGB, double-buffered "
                                 "FB config");
                return false;
        }

        data->glx_context = create_context(data, fb_config);

        if (data->glx_context == NULL)
                return false;

        visinfo = fv_gl.glXGetVisualFromFBConfig(data->display, fb_config);

        if (visinfo == NULL) {
                fv_error_message("FB config does not have an associated "
                                 "visual");
                fv_gl.glXDestroyContext(data->display, data->glx_context);
                return false;
        }

        /* window attributes */
        attr.background_pixel = 0;
        attr.border_pixel = 0;
        attr.colormap = XCreateColormap(data->display,
                                        root,
                                        visinfo->visual,
                                        AllocNone);
        attr.event_mask = (StructureNotifyMask | ExposureMask |
                           KeyPressMask | KeyReleaseMask |
                           ButtonPressMask | ButtonReleaseMask);
        mask = CWBorderPixel | CWColormap | CWEventMask;

        data->x_window = XCreateWindow(data->display, root, 0, 0, 800, 600,
                                       0, visinfo->depth, InputOutput,
                                       visinfo->visual, mask, &attr);

        XFree(visinfo);

        if (!data->x_window) {
                fv_error_message("XCreateWindow failed");
                fv_gl.glXDestroyContext(data->display, data->glx_context);
                return false;
        }

        data->glx_window = fv_gl.glXCreateWindow(data->display,
                                                 fb_config,
                                                 data->x_window,
                                                 NULL /* attrib_list */);

        fv_gl.glXMakeContextCurrent(data->display,
                                    data->glx_window,
                                    data->glx_window,
                                    data->glx_context);

        XMapWindow(data->display, data->x_window);

        return true;
}

static int
find_queue_family(struct data *data)
{
        VkPhysicalDevice physical_device = data->vk_data.physical_device;
        VkQueueFamilyProperties *queues;
        uint32_t count = 0;
        uint32_t i;

        fv_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       NULL /* queues */);

        queues = fv_alloc(sizeof *queues * count);

        fv_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       queues);

        for (i = 0; i < count; i++) {
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    queues[i].queueCount >= 1)
                        break;
        }

        fv_free(queues);

        if (i >= count)
                return -1;
        else
                return i;
}

static VkFormat
get_depth_format(struct data *data)
{
        /* According to the spec at least one of these formats must be
         * supported for depth so we'll just try them both until one
         * of them works.
         */
        static const VkFormat formats[] = {
                VK_FORMAT_X8_D24_UNORM_PACK32,
                VK_FORMAT_D32_SFLOAT
        };
        VkFormatProperties format_properties;
        VkPhysicalDevice physical_device = data->vk_data.physical_device;
        int i;

        for (i = 0; i < FV_N_ELEMENTS(formats); i++) {
                fv_vk.vkGetPhysicalDeviceFormatProperties(physical_device,
                                                          formats[i],
                                                          &format_properties);
                if ((format_properties.optimalTilingFeatures &
                     VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
                        return formats[i];
        }

        assert(false);
}

static bool
init_vk(struct data *data)
{
        VkPhysicalDeviceMemoryProperties *memory_properties =
                &data->vk_data.memory_properties;
        VkResult res;
        uint32_t count = 1;

        struct VkInstanceCreateInfo instance_create_info = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &(VkApplicationInfo) {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .pApplicationName = "finvenkisto",
                        .apiVersion = VK_MAKE_VERSION(1, 0, 2)
                }
        };
        res = fv_vk.vkCreateInstance(&instance_create_info,
                                     NULL, /* allocator */
                                     &data->vk_instance);

        if (res != VK_SUCCESS) {
                fv_error_message("Failed to create VkInstance");
                return false;
        }

        fv_vk_init_instance(data->vk_instance);

        res = fv_vk.vkEnumeratePhysicalDevices(data->vk_instance,
                                               &count,
                                               &data->vk_data.physical_device);
        if (res != VK_SUCCESS || count < 1) {
                fv_error_message("Error enumerating VkPhysicalDevices");
                goto error_instance;
        }

        fv_vk.vkGetPhysicalDeviceProperties(data->vk_data.physical_device,
                                            &data->vk_data.device_properties);
        fv_vk.vkGetPhysicalDeviceMemoryProperties(data->vk_data.physical_device,
                                                  memory_properties);
        data->vk_depth_format = get_depth_format(data);

        data->vk_data.queue_family = find_queue_family(data);
        if (data->vk_data.queue_family == -1) {
                fv_error_message("No graphics queue found on Vulkan device");
                goto error_instance;
        }

        VkPhysicalDeviceFeatures features;
        memset(&features, 0, sizeof features);

        VkDeviceCreateInfo device_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .queueFamilyIndex = data->vk_data.queue_family,
                        .queueCount = 1,
                        .pQueuePriorities = (float[]) { 1.0f }
                },
                .pEnabledFeatures = &features
        };
        res = fv_vk.vkCreateDevice(data->vk_data.physical_device,
                                   &device_create_info,
                                   NULL, /* allocator */
                                   &data->vk_data.device);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkDevice");
                goto error_instance;
        }

        fv_vk_init_device(data->vk_data.device);

        fv_vk.vkGetDeviceQueue(data->vk_data.device,
                               data->vk_data.queue_family,
                               0, /* queueIndex */
                               &data->vk_queue);

        VkCommandPoolCreateInfo command_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .queueFamilyIndex = data->vk_data.queue_family
        };
        res = fv_vk.vkCreateCommandPool(data->vk_data.device,
                                        &command_pool_create_info,
                                        NULL, /* allocator */
                                        &data->vk_command_pool);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkCommandPool");
                goto error_device;
        }

        VkAttachmentDescription attachment_descriptions[] = {
                {
                        .format = COLOR_IMAGE_FORMAT,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                },
                {
                        .format = data->vk_depth_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout = VK_IMAGE_LAYOUT_UNDEFINED
                },
        };
        VkSubpassDescription subpass_descriptions[] = {
                {
                        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &(VkAttachmentReference) {
                                .attachment = 0,
                                .layout =
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                        },
                        .pDepthStencilAttachment = &(VkAttachmentReference) {
                                .attachment = 1,
                                .layout =
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                        }
                }
        };
        VkRenderPassCreateInfo render_pass_create_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = FV_N_ELEMENTS(attachment_descriptions),
                .pAttachments = attachment_descriptions,
                .subpassCount = FV_N_ELEMENTS(subpass_descriptions),
                .pSubpasses = subpass_descriptions
        };
        res = fv_vk.vkCreateRenderPass(data->vk_data.device,
                                       &render_pass_create_info,
                                       NULL, /* allocator */
                                       &data->vk_render_pass);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating render pass");
                goto error_command_pool;
        }

        VkFenceCreateInfo fence_create_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        res = fv_vk.vkCreateFence(data->vk_data.device,
                                  &fence_create_info,
                                  NULL, /* allocator */
                                  &data->vk_fence);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating fence");
                goto error_render_pass;
        }

        memset(&data->vk_fb, 0, sizeof data->vk_fb);

        return true;

error_render_pass:
        fv_vk.vkDestroyRenderPass(data->vk_data.device,
                                  data->vk_render_pass,
                                  NULL /* allocator */);
error_command_pool:
        fv_vk.vkDestroyCommandPool(data->vk_data.device,
                                   data->vk_command_pool,
                                   NULL /* allocator */);
error_device:
        fv_vk.vkDestroyDevice(data->vk_data.device,
                              NULL /* allocator */);
error_instance:
        fv_vk.vkDestroyInstance(data->vk_instance,
                                NULL /* allocator */);
        return false;
}

static void
deinit_vk(struct data *data)
{
        fv_vk.vkDestroyFence(data->vk_data.device,
                             data->vk_fence,
                             NULL /* allocator */);
        fv_vk.vkDestroyRenderPass(data->vk_data.device,
                                  data->vk_render_pass,
                                  NULL /* allocator */);
        fv_vk.vkDestroyCommandPool(data->vk_data.device,
                                   data->vk_command_pool,
                                   NULL /* allocator */);
        fv_vk.vkDestroyDevice(data->vk_data.device,
                              NULL /* allocator */);
        fv_vk.vkDestroyInstance(data->vk_instance,
                                NULL /* allocator */);
}

int
main(int argc, char **argv)
{
        struct data data;
        int ret = EXIT_SUCCESS;

        data.is_fullscreen = true;
        data.window_mapped = false;
        data.fb_width = 0;
        data.fb_height = 0;

        fv_data_init(argv[0]);

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out_data;
        }

        if (!fv_gl_load_libgl()) {
                ret = EXIT_FAILURE;
                goto out_data;
        }

        if (!fv_vk_load_libvulkan()) {
                ret = EXIT_FAILURE;
                goto out_libgl;
        }

        if (!init_vk(&data)) {
                ret = EXIT_FAILURE;
                goto out_libvulkan;
        }

        data.display = XOpenDisplay(NULL);
        if (data.display == NULL) {
                fv_error_message("Error: XOpenDisplay failed");
                ret = EXIT_FAILURE;
                goto out_vk;
        }

        if (!fv_gl_init_glx(data.display)) {
                ret = EXIT_FAILURE;
                goto out_display;
        }

        if (!make_window(&data)) {
                ret = EXIT_FAILURE;
                goto out_display;
        }

        fv_gl_init();

        if (!check_gl_version()) {
                ret = EXIT_FAILURE;
                goto out_window;
        }

        data.quit = false;

        data.logic = fv_logic_new();

        data.image_data = fv_image_data_old_new();

        if (data.image_data == NULL) {
                ret = EXIT_FAILURE;
                goto out_logic;
        }

        if (!fv_pipeline_data_init(&data.vk_data,
                                   data.vk_render_pass,
                                   &data.pipeline_data))
                goto out_image_data;

        if (!create_graphics(&data)) {
                ret = EXIT_FAILURE;
                goto out_pipeline_data;
        }

        data.blit_program = create_blit_program();

        reset_menu_state(&data);

        while (!data.quit)
                iterate_main_loop(&data);

        fv_gl.glDeleteProgram(data.blit_program);

        destroy_framebuffer_resources(&data);

        destroy_graphics(&data);

out_pipeline_data:
        fv_pipeline_data_destroy(&data.vk_data,
                                 &data.pipeline_data);
out_image_data:
        fv_image_data_old_free(data.image_data);
out_logic:
        fv_logic_free(data.logic);
out_window:
        fv_gl.glXMakeContextCurrent(data.display, None, None, NULL);
        fv_gl.glXDestroyContext(data.display, data.glx_context);
        fv_gl.glXDestroyWindow(data.display, data.glx_window);
        XDestroyWindow(data.display, data.x_window);
out_display:
        XCloseDisplay(data.display);
out_vk:
        deinit_vk(&data);
out_libvulkan:
        fv_vk_unload_libvulkan();
out_libgl:
        fv_gl_unload_libgl();
out_data:
        fv_data_deinit();

        return ret;
}
