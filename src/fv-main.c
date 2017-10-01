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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-image-data.h"
#include "fv-vk.h"
#include "fv-util.h"
#include "fv-hud.h"
#include "fv-buffer.h"
#include "fv-map.h"
#include "fv-error-message.h"
#include "fv-data.h"
#include "fv-pipeline-data.h"
#include "fv-vk-data.h"
#include "fv-allocate-store.h"

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

struct swapchain_image {
        VkImage image;
        VkImageView image_view;
        VkFramebuffer framebuffer;
};

struct data {
        Display *display;
        Window x_window;

        /* Permanant vulkan resources */
        struct fv_vk_data vk_data;
        VkInstance vk_instance;
        VkFormat vk_depth_format;
        VkQueue vk_queue;
        VkCommandPool vk_command_pool;
        VkCommandBuffer vk_command_buffer;
        VkRenderPass vk_render_pass;
        VkFence vk_fence;
        VkSurfaceKHR vk_surface;
        VkSemaphore vk_semaphore;
        VkFormat vk_surface_format;
        VkPresentModeKHR vk_present_mode;

        /* Resources that are recreated lazily whenever the
         * framebuffer size changes.
         */
        struct {
                VkSwapchainKHR swapchain;
                uint32_t n_swapchain_images;
                struct swapchain_image *swapchain_images;
                VkImage depth_image;
                VkDeviceMemory depth_image_memory;
                VkImageView depth_image_view;
                int width, height;
        } vk_fb;

        bool window_mapped;
        int fb_width, fb_height;
        int last_fb_width, last_fb_height;

        struct {
                struct fv_game *game;
                struct fv_hud *hud;
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
                .commandPool = data->vk_command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
        };
        res = fv_vk.vkAllocateCommandBuffers(data->vk_data.device,
                                             &command_buffer_allocate_info,
                                             &command_buffer);
        if (res != VK_SUCCESS)
                return false;

        VkCommandBufferBeginInfo command_buffer_begin_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        fv_vk.vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

        image_data = fv_image_data_new(&data->vk_data, command_buffer);
        if (image_data == NULL)
                goto error_command_buffer;

        memset(&data->graphics, 0, sizeof data->graphics);

        data->last_fb_width = data->last_fb_height = 0;

        data->graphics.hud = fv_hud_new(&data->vk_data,
                                        &data->pipeline_data,
                                        image_data);

        if (data->graphics.hud == NULL)
                goto error;

        data->graphics.game = fv_game_new(&data->vk_data,
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
        fv_vk.vkQueueSubmit(data->vk_queue, 1, &submitInfo, VK_NULL_HANDLE);

        fv_vk.vkQueueWaitIdle(data->vk_queue);

        fv_image_data_free(image_data);

        fv_vk.vkFreeCommandBuffers(data->vk_data.device,
                                   data->vk_command_pool,
                                   1, /* commandBufferCount */
                                   &command_buffer);

        return true;

error:
        destroy_graphics(data);
        fv_image_data_free(image_data);
error_command_buffer:
        fv_vk.vkFreeCommandBuffers(data->vk_data.device,
                                   data->vk_command_pool,
                                   1, /* commandBufferCount */
                                   &command_buffer);
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
                fv_hud_paint_player_select(data->graphics.hud,
                                           data->vk_command_buffer,
                                           w, h);
                break;
        case MENU_STATE_CHOOSING_KEYS:
                fv_hud_paint_key_select(data->graphics.hud,
                                        data->vk_command_buffer,
                                        w, h,
                                        data->next_player,
                                        data->next_key,
                                        data->n_players);
                break;
        case MENU_STATE_PLAYING:
                fv_hud_paint_game_state(data->graphics.hud,
                                        data->vk_command_buffer,
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

static void
destroy_swapchain_image(struct data *data,
                        struct swapchain_image *swapchain_image)
{
        if (swapchain_image->framebuffer) {
                fv_vk.vkDestroyFramebuffer(data->vk_data.device,
                                           swapchain_image->framebuffer,
                                           NULL /* allocator */);
        }
        if (swapchain_image->image_view) {
                fv_vk.vkDestroyImageView(data->vk_data.device,
                                         swapchain_image->image_view,
                                         NULL /* allocator */);
        }
}

static void
destroy_framebuffer_resources(struct data *data)
{
        int i;

        if (data->vk_fb.depth_image_view)
                fv_vk.vkDestroyImageView(data->vk_data.device,
                                         data->vk_fb.depth_image_view,
                                         NULL /* allocator */);
        if (data->vk_fb.depth_image_memory)
                fv_vk.vkFreeMemory(data->vk_data.device,
                                   data->vk_fb.depth_image_memory,
                                   NULL /* allocator */);
        if (data->vk_fb.depth_image)
                fv_vk.vkDestroyImage(data->vk_data.device,
                                     data->vk_fb.depth_image,
                                     NULL /* allocator */);
        if (data->vk_fb.swapchain_images) {
                for (i = 0; i < data->vk_fb.n_swapchain_images; i++) {
                        destroy_swapchain_image(data,
                                                data->vk_fb.swapchain_images +
                                                i);
                }
                fv_free(data->vk_fb.swapchain_images);
        }
        if (data->vk_fb.swapchain)
                fv_vk.vkDestroySwapchainKHR(data->vk_data.device,
                                            data->vk_fb.swapchain,
                                            NULL /* allocator */);

        memset(&data->vk_fb, 0, sizeof data->vk_fb);
}

static bool
create_swapchain_image(struct data *data,
                       struct swapchain_image *swapchain_image)
{
        VkResult res;

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapchain_image->image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = data->vk_surface_format,
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
                                      &swapchain_image->image_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating image view");
                goto error;
        }

        VkImageView attachments[] = {
                swapchain_image->image_view,
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
                                        &swapchain_image->framebuffer);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating framebuffer");
                goto error;
        }

        return true;

error:
        return false;
}

static bool
create_swapchain_images(struct data *data)
{
        VkResult res;
        VkImage *images;
        uint32_t n_images;
        int i;

        res = fv_vk.vkGetSwapchainImagesKHR(data->vk_data.device,
                                            data->vk_fb.swapchain,
                                            &n_images,
                                            NULL /* images */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting swapchain images");
                goto error;
        }
        images = alloca(sizeof *images * n_images);
        res = fv_vk.vkGetSwapchainImagesKHR(data->vk_data.device,
                                            data->vk_fb.swapchain,
                                            &n_images,
                                            images);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting swapchain images");
                goto error;
        }

        data->vk_fb.swapchain_images =
                fv_calloc(sizeof *data->vk_fb.swapchain_images * n_images);
        data->vk_fb.n_swapchain_images = n_images;

        for (i = 0; i < n_images; i++) {
                data->vk_fb.swapchain_images[i].image = images[i];
                if (!create_swapchain_image(data,
                                            data->vk_fb.swapchain_images + i))
                        goto error;
        }

        return true;

error:
        return false;
}

static bool
create_framebuffer_resources(struct data *data)
{
        VkResult res;

        VkSwapchainCreateInfoKHR swapchain_create_info = {
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                .surface = data->vk_surface,
                .minImageCount = 2,
                .imageFormat = data->vk_surface_format,
                .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
                .imageExtent = { data->fb_width, data->fb_height },
                .imageArrayLayers = 1,
                .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 1,
                .pQueueFamilyIndices =
                (uint32_t[]) { data->vk_data.queue_family },
                .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                .presentMode = data->vk_present_mode,
                .clipped = VK_TRUE
        };
        res = fv_vk.vkCreateSwapchainKHR(data->vk_data.device,
                                         &swapchain_create_info,
                                         NULL, /* allocator */
                                         &data->vk_fb.swapchain);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating swapchain");
                goto error;
        }

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = data->vk_depth_format,
                .extent = {
                        .width = data->fb_width,
                        .height = data->fb_height,
                        .depth = 1
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        res = fv_vk.vkCreateImage(data->vk_data.device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &data->vk_fb.depth_image);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating depth image");
                goto error;
        }

        res = fv_allocate_store_image(&data->vk_data,
                                      0, /* memory_type_flags */
                                      1, /* n_images */
                                      &data->vk_fb.depth_image,
                                      &data->vk_fb.depth_image_memory,
                                      NULL /* memory_type_index */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating depthbuffer memory");
                goto error;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = data->vk_fb.depth_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = data->vk_depth_format,
                .components = {
                        .r = VK_COMPONENT_SWIZZLE_R,
                        .g = VK_COMPONENT_SWIZZLE_G,
                        .b = VK_COMPONENT_SWIZZLE_B,
                        .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = fv_vk.vkCreateImageView(data->vk_data.device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &data->vk_fb.depth_image_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating depth-stencil image view");
                goto error;
        }

        if (!create_swapchain_images(data))
                goto error;

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
        VkResult res;
        uint32_t swapchain_image_index;
        struct swapchain_image *swapchain_image;
        int i;

        if (data->vk_fb.width != data->fb_width ||
            data->vk_fb.height != data->fb_height) {
                destroy_framebuffer_resources(data);
                if (!create_framebuffer_resources(data)) {
                        data->quit = true;
                        return;
                }
        }

        res = fv_vk.vkAcquireNextImageKHR(data->vk_data.device,
                                          data->vk_fb.swapchain,
                                          UINT64_MAX,
                                          data->vk_semaphore,
                                          VK_NULL_HANDLE, /* fence */
                                          &swapchain_image_index);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting swapchain image");
                data->quit = true;
                return;
        }

        swapchain_image = data->vk_fb.swapchain_images + swapchain_image_index;

        VkCommandBufferBeginInfo begin_command_buffer_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        res = fv_vk.vkBeginCommandBuffer(data->vk_command_buffer,
                                         &begin_command_buffer_info);
        if (res != VK_SUCCESS)
                return;

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
                .framebuffer = swapchain_image->framebuffer,
                .renderArea = {
                        .offset = { 0, 0 },
                        .extent = { data->fb_width, data->fb_height}
                },
                .clearValueCount = FV_N_ELEMENTS(clear_values),
                .pClearValues = clear_values
        };
        fv_vk.vkCmdBeginRenderPass(data->vk_command_buffer,
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
                fv_vk.vkCmdClearAttachments(data->vk_command_buffer,
                                            1, /* attachmentCount */
                                            &color_clear_attachment,
                                            1,
                                            &color_clear_rect);
        }

        VkRect2D scissor = {
                .offset = { .x = 0, .y = 0 },
                .extent = { .width = data->fb_width, .height = data->fb_height }
        };
        fv_vk.vkCmdSetScissor(data->vk_command_buffer,
                              0, /* firstScissor */
                              1, /* scissorCount */
                              &scissor);

        fv_game_begin_frame(data->graphics.game);

        for (i = 0; i < data->n_viewports; i++) {
                VkViewport viewport = {
                        .x = data->players[i].viewport_x,
                        .y = data->players[i].viewport_y,
                        .width = data->players[i].viewport_width,
                        .height = data->players[i].viewport_height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                };
                fv_vk.vkCmdSetViewport(data->vk_command_buffer,
                                       0, /* firstViewport */
                                       1, /* viewportCount */
                                       &viewport);
                fv_game_paint(data->graphics.game,
                              data->players[i].center_x,
                              data->players[i].center_y,
                              data->players[i].viewport_width,
                              data->players[i].viewport_height,
                              data->logic,
                              data->vk_command_buffer);
        }

        fv_game_end_frame(data->graphics.game);

        if (data->n_viewports != 1) {
                VkViewport viewport = {
                        .x = 0,
                        .y = 0,
                        .width = data->fb_width,
                        .height = data->fb_height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                };
                fv_vk.vkCmdSetViewport(data->vk_command_buffer,
                                       0, /* firstViewport */
                                       1, /* viewportCount */
                                       &viewport);
        }

        paint_hud(data, data->fb_width, data->fb_height);

        fv_vk.vkCmdEndRenderPass(data->vk_command_buffer);

        res = fv_vk.vkEndCommandBuffer(data->vk_command_buffer);
        if (res != VK_SUCCESS)
                return;

        fv_vk.vkResetFences(data->vk_data.device,
                            1, /* fenceCount */
                            &data->vk_fence);

        VkSubmitInfo submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &data->vk_command_buffer,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = (VkSemaphore[]) { data->vk_semaphore },
                .pWaitDstStageMask =
                (VkPipelineStageFlagBits[])
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT }
        };
        res = fv_vk.vkQueueSubmit(data->vk_queue,
                                  1, /* submitCount */
                                  &submit_info,
                                  data->vk_fence);
        if (res != VK_SUCCESS)
                return;

        res = fv_vk.vkWaitForFences(data->vk_data.device,
                                    1, /* fenceCount */
                                    &data->vk_fence,
                                    VK_TRUE, /* waitAll */
                                    UINT64_MAX);
        if (res != VK_SUCCESS)
                return;

        VkPresentInfoKHR present_info = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .swapchainCount = 1,
                .pSwapchains = (VkSwapchainKHR[]) { data->vk_fb.swapchain },
                .pImageIndices = (uint32_t[]) { swapchain_image_index },
        };
        res = fv_vk.vkQueuePresentKHR(data->vk_queue,
                                      &present_info);
        if (res != VK_SUCCESS) {
                fv_error_message("Error presenting image");
                data->quit = true;
                return;
        }
}

static void
paint(struct data *data)
{
        if (data->fb_width != data->last_fb_width ||
            data->fb_height != data->last_fb_height) {
                data->last_fb_width = data->fb_width;
                data->last_fb_height = data->fb_height;
                data->viewports_dirty = true;
        }

        fv_logic_update(data->logic, get_ticks(data));

        update_viewports(data);
        update_centers(data);

        paint_vk(data);
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

static bool
make_window(struct data *data)
{
        XSetWindowAttributes attr;
        unsigned long mask;
        Window root;
        Visual *visual;

        root = RootWindow(data->display, 0 /* screen */);
        visual = DefaultVisual(data->display, 0 /* screen */);

        /* window attributes */
        attr.background_pixel = 0;
        attr.border_pixel = 0;
        attr.colormap = XCreateColormap(data->display,
                                        root,
                                        visual,
                                        AllocNone);
        attr.event_mask = (StructureNotifyMask | ExposureMask |
                           KeyPressMask | KeyReleaseMask |
                           ButtonPressMask | ButtonReleaseMask);
        mask = CWBorderPixel | CWColormap | CWEventMask;

        data->x_window = XCreateWindow(data->display,
                                       root, /* parent */
                                       0, 0, 800, 600, /* x/y/width/height */
                                       0, /* border_width */
                                       CopyFromParent, /* depth */
                                       InputOutput,
                                       visual,
                                       mask,
                                       &attr);

        if (!data->x_window) {
                fv_error_message("XCreateWindow failed");
                return false;
        }

        XMapWindow(data->display, data->x_window);

        return true;
}

static int
find_queue_family(struct data *data,
                  VkPhysicalDevice physical_device)
{
        VkQueueFamilyProperties *queues;
        uint32_t count = 0;
        uint32_t i;
        VkBool32 supported;

        fv_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       NULL /* queues */);

        queues = fv_alloc(sizeof *queues * count);

        fv_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       queues);

        for (i = 0; i < count; i++) {
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 ||
                    queues[i].queueCount < 1)
                        continue;

                supported = false;
                fv_vk.vkGetPhysicalDeviceSurfaceSupportKHR(physical_device,
                                                           i,
                                                           data->vk_surface,
                                                           &supported);
                if (supported)
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

static void
deinit_vk(struct data *data)
{
        if (data->vk_fence) {
                fv_vk.vkDestroyFence(data->vk_data.device,
                                     data->vk_fence,
                                     NULL /* allocator */);
        }
        if (data->vk_render_pass) {
                fv_vk.vkDestroyRenderPass(data->vk_data.device,
                                          data->vk_render_pass,
                                          NULL /* allocator */);
        }
        if (data->vk_data.descriptor_pool) {
                fv_vk.vkDestroyDescriptorPool(data->vk_data.device,
                                              data->vk_data.descriptor_pool,
                                              NULL /* allocator */);
        }
        if (data->vk_command_buffer) {
                fv_vk.vkFreeCommandBuffers(data->vk_data.device,
                                           data->vk_command_pool,
                                           1, /* commandBufferCount */
                                           &data->vk_command_buffer);
        }
        if (data->vk_command_pool) {
                fv_vk.vkDestroyCommandPool(data->vk_data.device,
                                           data->vk_command_pool,
                                           NULL /* allocator */);
        }
        if (data->vk_semaphore) {
                fv_vk.vkDestroySemaphore(data->vk_data.device,
                                         data->vk_semaphore,
                                         NULL /* allocator */);
        }
        if (data->vk_data.device) {
                fv_vk.vkDestroyDevice(data->vk_data.device,
                                      NULL /* allocator */);
        }
        if (data->vk_surface) {
                fv_vk.vkDestroySurfaceKHR(data->vk_instance,
                                          data->vk_surface,
                                          NULL /* allocator */);
        }
        if (data->vk_instance) {
                fv_vk.vkDestroyInstance(data->vk_instance,
                                        NULL /* allocator */);
        }
}

static bool
check_device_extension(struct data *data,
                       VkPhysicalDevice physical_device,
                       const char *extension)
{
        VkExtensionProperties *extensions;
        uint32_t count;
        VkResult res;
        int i;

        res = fv_vk.vkEnumerateDeviceExtensionProperties(physical_device,
                                                         NULL, /* layerName */
                                                         &count,
                                                         NULL /* properties */);
        if (res != VK_SUCCESS)
                return false;

        extensions = alloca(sizeof *extensions * count);

        res = fv_vk.vkEnumerateDeviceExtensionProperties(physical_device,
                                                         NULL, /* layerName */
                                                         &count,
                                                         extensions);
        if (res != VK_SUCCESS)
                return false;

        for (i = 0; i < count; i++) {
                if (!strcmp(extensions[i].extensionName, extension))
                        return true;
        }

        return false;
}

static bool
check_physical_device_surface_capabilities(struct data *data,
                                           VkPhysicalDevice physical_device)
{
        VkSurfaceCapabilitiesKHR caps;
        VkResult res;

        res = fv_vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device,
                                                              data->vk_surface,
                                                              &caps);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting device surface caps");
                return false;
        }

        if (caps.maxImageCount != 0 && caps.maxImageCount < 2)
                return false;
        if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
                return false;
        if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
                return false;
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
                return false;

        return true;
}

static bool
find_physical_device(struct data *data)
{
        VkResult res;
        uint32_t count;
        VkPhysicalDevice *devices;
        int i, queue_family;

        res = fv_vk.vkEnumeratePhysicalDevices(data->vk_instance,
                                               &count,
                                               NULL);
        if (res != VK_SUCCESS) {
                fv_error_message("Error enumerating VkPhysicalDevices");
                return false;
        }

        devices = alloca(count * sizeof *devices);

        res = fv_vk.vkEnumeratePhysicalDevices(data->vk_instance,
                                               &count,
                                               devices);
        if (res != VK_SUCCESS) {
                fv_error_message("Error enumerating VkPhysicalDevices");
                return false;
        }

        for (i = 0; i < count; i++) {
                if (!check_device_extension(data,
                                            devices[i],
                                            VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                        continue;

                queue_family = find_queue_family(data, devices[i]);
                if (queue_family == -1)
                        continue;

                if (!check_physical_device_surface_capabilities(data,
                                                                devices[i]))
                        continue;

                data->vk_data.physical_device = devices[i];
                data->vk_data.queue_family = queue_family;

                return true;
        }

        fv_error_message("No suitable device and queue family found");
        return false;
}

static bool
find_surface_format(struct data *data)
{
        VkPhysicalDevice physical_device = data->vk_data.physical_device;
        VkSurfaceFormatKHR *formats;
        uint32_t count = 0;
        VkResult res;
        int i;

        res = fv_vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device,
                                                         data->vk_surface,
                                                         &count,
                                                         NULL /* formats */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported surface formats");
                return false;
        }

        formats = alloca(sizeof *formats * count);

        res = fv_vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device,
                                                         data->vk_surface,
                                                         &count,
                                                         formats);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported surface formats");
                return false;
        }

        for (i = 0; i < count; i++) {
                switch (formats[i].format) {
                case VK_FORMAT_B8G8R8A8_UNORM:
                case VK_FORMAT_R8G8B8A8_UNORM:
                        data->vk_surface_format = formats[i].format;
                        return true;
                default:
                        continue;
                }
        }

        fv_error_message("No suitable surface format found");
        return false;
}

static bool
find_present_mode(struct data *data)
{
        static const VkPresentModeKHR mode_preference[] = {
                VK_PRESENT_MODE_MAILBOX_KHR,
                VK_PRESENT_MODE_FIFO_KHR
        };
        VkPhysicalDevice physical_device = data->vk_data.physical_device;
        VkPresentModeKHR *present_modes;
        int chosen_preference = -1;
        uint32_t count = 0;
        VkResult res;
        int i, j;

        res = fv_vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                                              data->vk_surface,
                                                              &count,
                                                              NULL);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported present modes");
                return false;
        }

        present_modes = alloca(sizeof *present_modes * count);

        res = fv_vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                                              data->vk_surface,
                                                              &count,
                                                              present_modes);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported present modes");
                return false;
        }

        for (i = 0; i < count; i++) {
                for (j = 0; j < FV_N_ELEMENTS(mode_preference); j++) {
                        if (mode_preference[j] == present_modes[i]) {
                                if (j > chosen_preference)
                                        chosen_preference = j;
                                break;
                        }
                }
        }

        if (chosen_preference == -1) {
                fv_error_message("No suitable present mode found");
                return false;
        }

        data->vk_present_mode = mode_preference[chosen_preference];
        return true;
}

static bool
init_vk(struct data *data)
{
        VkPhysicalDeviceMemoryProperties *memory_properties =
                &data->vk_data.memory_properties;
        VkResult res;

        struct VkInstanceCreateInfo instance_create_info = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &(VkApplicationInfo) {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .pApplicationName = "finvenkisto",
                        .apiVersion = VK_MAKE_VERSION(1, 0, 2)
                },
                .enabledExtensionCount = 2,
                .ppEnabledExtensionNames = (const char * const []) {
                        VK_KHR_SURFACE_EXTENSION_NAME,
                        VK_KHR_XLIB_SURFACE_EXTENSION_NAME
                },
        };
        res = fv_vk.vkCreateInstance(&instance_create_info,
                                     NULL, /* allocator */
                                     &data->vk_instance);

        if (res != VK_SUCCESS) {
                fv_error_message("Failed to create VkInstance");
                goto error;
        }

        fv_vk_init_instance(data->vk_instance);

        VkXlibSurfaceCreateInfoKHR xlib_surface_create_info = {
                .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                .dpy = data->display,
                .window = data->x_window,
        };
        res = fv_vk.vkCreateXlibSurfaceKHR(data->vk_instance,
                                           &xlib_surface_create_info,
                                           NULL, /* allocator */
                                           &data->vk_surface);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating xlib surface");
                goto error;
        }

        if (!find_physical_device(data))
                goto error;

        fv_vk.vkGetPhysicalDeviceProperties(data->vk_data.physical_device,
                                            &data->vk_data.device_properties);
        fv_vk.vkGetPhysicalDeviceMemoryProperties(data->vk_data.physical_device,
                                                  memory_properties);

        data->vk_depth_format = get_depth_format(data);

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
                .pEnabledFeatures = &features,
                .enabledExtensionCount = 1,
                .ppEnabledExtensionNames = (const char * const []) {
                        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                },
        };
        res = fv_vk.vkCreateDevice(data->vk_data.physical_device,
                                   &device_create_info,
                                   NULL, /* allocator */
                                   &data->vk_data.device);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkDevice");
                goto error;
        }

        fv_vk_init_device(data->vk_data.device);

        fv_vk.vkGetDeviceQueue(data->vk_data.device,
                               data->vk_data.queue_family,
                               0, /* queueIndex */
                               &data->vk_queue);

        VkSemaphoreCreateInfo semaphore_create_info = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        res = fv_vk.vkCreateSemaphore(data->vk_data.device,
                                      &semaphore_create_info,
                                      NULL, /* allocator */
                                      &data->vk_semaphore);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating semaphore");
                goto error;
        }

        if (!find_surface_format(data))
                goto error;

        if (!find_present_mode(data))
                goto error;

        VkCommandPoolCreateInfo command_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = data->vk_data.queue_family
        };
        res = fv_vk.vkCreateCommandPool(data->vk_data.device,
                                        &command_pool_create_info,
                                        NULL, /* allocator */
                                        &data->vk_command_pool);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkCommandPool");
                goto error;
        }

        VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = data->vk_command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
        };
        res = fv_vk.vkAllocateCommandBuffers(data->vk_data.device,
                                             &command_buffer_allocate_info,
                                             &data->vk_command_buffer);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating command buffer");
                goto error;
        }

        VkDescriptorPoolSize pool_size = {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 4
        };
        VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets = 4,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size
        };
        res = fv_vk.vkCreateDescriptorPool(data->vk_data.device,
                                           &descriptor_pool_create_info,
                                           NULL, /* allocator */
                                           &data->vk_data.descriptor_pool);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkDescriptorPool");
                goto error;
        }

        VkAttachmentDescription attachment_descriptions[] = {
                {
                        .format = data->vk_surface_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                },
                {
                        .format = data->vk_depth_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout =
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
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
                goto error;
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
                goto error;
        }

        memset(&data->vk_fb, 0, sizeof data->vk_fb);

        return true;

error:
        deinit_vk(data);
        return false;
}

int
main(int argc, char **argv)
{
        struct data data;
        int ret = EXIT_SUCCESS;

        memset(&data, 0, sizeof data);

        data.is_fullscreen = true;
        data.window_mapped = false;
        data.fb_width = 0;
        data.fb_height = 0;

        fv_data_init(argv[0]);

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out_data;
        }

        if (!fv_vk_load_libvulkan()) {
                ret = EXIT_FAILURE;
                goto out_data;
        }

        data.display = XOpenDisplay(NULL);
        if (data.display == NULL) {
                fv_error_message("Error: XOpenDisplay failed");
                ret = EXIT_FAILURE;
                goto out_libvulkan;
        }

        if (!make_window(&data)) {
                ret = EXIT_FAILURE;
                goto out_display;
        }

        if (!init_vk(&data)) {
                ret = EXIT_FAILURE;
                goto out_window;
        }

        data.quit = false;

        data.logic = fv_logic_new();

        if (!fv_pipeline_data_init(&data.vk_data,
                                   data.vk_render_pass,
                                   &data.pipeline_data))
                goto out_logic;

        if (!create_graphics(&data)) {
                ret = EXIT_FAILURE;
                goto out_pipeline_data;
        }

        reset_menu_state(&data);

        while (!data.quit)
                iterate_main_loop(&data);

        destroy_framebuffer_resources(&data);

        destroy_graphics(&data);

out_pipeline_data:
        fv_pipeline_data_destroy(&data.vk_data,
                                 &data.pipeline_data);
out_logic:
        fv_logic_free(data.logic);
        deinit_vk(&data);
out_window:
        XDestroyWindow(data.display, data.x_window);
out_display:
        XCloseDisplay(data.display);
out_libvulkan:
        fv_vk_unload_libvulkan();
out_data:
        fv_data_deinit();

        return ret;
}
