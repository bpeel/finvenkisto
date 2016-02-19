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
#include <SDL.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <GL/glx.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-image-data.h"
#include "fv-shader-data.h"
#include "fv-gl.h"
#include "fv-util.h"
#include "fv-hud.h"
#include "fv-buffer.h"
#include "fv-map.h"
#include "fv-error-message.h"

#define MIN_GL_MAJOR_VERSION 2
#define MIN_GL_MINOR_VERSION 0
#define REQUEST_GL_MAJOR_VERSION MIN_GL_MAJOR_VERSION
#define REQUEST_GL_MINOR_VERSION MIN_GL_MINOR_VERSION
#define CORE_GL_MAJOR_VERSION 3
#define CORE_GL_MINOR_VERSION 1
#define FV_GL_PROFILE SDL_GL_CONTEXT_PROFILE_COMPATIBILITY

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
        KEY_TYPE_JOYSTICK,
        KEY_TYPE_MOUSE
};

struct key {
        enum key_type type;
        SDL_Keycode keycode;
        SDL_JoystickID device_id;
        Uint8 button;
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
        struct fv_image_data *image_data;
        Uint32 image_data_event;

        Display *display;
        Window x_window;
        GLXWindow glx_window;
        GLXContext glx_context;

        SDL_Window *window;
        int last_fb_width, last_fb_height;
        SDL_GLContext gl_context;

        struct {
                struct fv_shader_data shader_data;
                struct fv_game *game;
                struct fv_hud *hud;
                bool shader_data_loaded;
        } graphics;

        struct fv_logic *logic;

        bool quit;
        bool is_fullscreen;

        bool viewports_dirty;
        int n_viewports;

        unsigned int start_time;

        enum menu_state menu_state;
        int n_players;
        int next_player;
        enum key_code next_key;

        struct fv_buffer joysticks;

        struct player players[FV_LOGIC_MAX_PLAYERS];
};

static void
reset_menu_state(struct data *data)
{
        int i, j;

        data->menu_state = MENU_STATE_CHOOSING_N_PLAYERS;
        data->start_time = SDL_GetTicks();
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
toggle_fullscreen(struct data *data)
{
        int display_index;
        SDL_DisplayMode mode;

        display_index = SDL_GetWindowDisplayIndex(data->window);

        if (display_index == -1)
                return;

        if (SDL_GetDesktopDisplayMode(display_index, &mode) == -1)
                return;

        SDL_SetWindowDisplayMode(data->window, &mode);

        data->is_fullscreen = !data->is_fullscreen;

        SDL_SetWindowFullscreen(data->window, data->is_fullscreen);
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
                return key->keycode == other_key->keycode;

        case KEY_TYPE_JOYSTICK:
        case KEY_TYPE_MOUSE:
                return (key->device_id == other_key->device_id &&
                        key->button == other_key->button);
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
                        data->start_time = SDL_GetTicks();
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
                 const SDL_KeyboardEvent *event)
{
        struct key key;

        if (data->menu_state == MENU_STATE_CHOOSING_N_PLAYERS) {
                if (event->state == SDL_PRESSED &&
                    event->keysym.sym >= SDLK_1 &&
                    event->keysym.sym < SDLK_1 + FV_LOGIC_MAX_PLAYERS) {
                        data->n_players = event->keysym.sym - SDLK_1 + 1;
                        data->next_player = 0;
                        data->next_key = 0;
                        data->menu_state = MENU_STATE_CHOOSING_KEYS;
                        data->viewports_dirty = true;
                }

                return;
        }

        key.type = KEY_TYPE_KEYBOARD;
        key.keycode = event->keysym.sym;
        key.down = event->state == SDL_PRESSED;

        handle_key(data, &key);
}

static void
handle_key_event(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        switch (event->keysym.sym) {
        case SDLK_ESCAPE:
                if (event->state == SDL_PRESSED) {
                        if (data->menu_state == MENU_STATE_CHOOSING_N_PLAYERS)
                                data->quit = true;
                        else
                                reset_menu_state(data);
                }
                break;

        case SDLK_F11:
                if (event->state == SDL_PRESSED)
                        toggle_fullscreen(data);
                break;

        default:
                handle_other_key(data, event);
                break;
        }
}

static void
handle_joystick_button(struct data *data,
                       const SDL_JoyButtonEvent *event)
{
        struct key key;

        key.type = KEY_TYPE_JOYSTICK;
        key.device_id = event->which;
        key.button = event->button;
        key.down = event->state == SDL_PRESSED;

        handle_key(data, &key);
}

static void
handle_joystick_added(struct data *data,
                      const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick *joystick = SDL_JoystickOpen(event->which);
        SDL_Joystick **joysticks = (SDL_Joystick **) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (SDL_Joystick *);
        int i;

        if (joystick == NULL) {
                fprintf(stderr, "failed to open joystick %i: %s\n",
                        event->which,
                        SDL_GetError());
                return;
        }

        /* Check if we already have this joystick open */
        for (i = 0; i < n_joysticks; i++) {
                if (SDL_JoystickInstanceID(joysticks[i]) ==
                    SDL_JoystickInstanceID(joystick)) {
                        SDL_JoystickClose(joystick);
                        return;
                }
        }

        fv_buffer_append(&data->joysticks, &joystick, sizeof joystick);
}

static void
handle_joystick_removed(struct data *data,
                        const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick **joysticks = (SDL_Joystick **) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (SDL_Joystick *);
        int i;

        for (i = 0; i < n_joysticks; i++) {
                if (SDL_JoystickInstanceID(joysticks[i]) == event->which) {
                        SDL_JoystickClose(joysticks[i]);
                        if (i < n_joysticks - 1)
                                joysticks[i] = joysticks[n_joysticks - 1];
                        data->joysticks.length -= sizeof (SDL_Joystick *);
                        break;
                }
        }
}

static void
handle_mouse_button(struct data *data,
                    const SDL_MouseButtonEvent *event)
{
        struct key key;

        key.type = KEY_TYPE_MOUSE;
        key.device_id = event->which;
        key.button = event->button;
        key.down = event->state == SDL_PRESSED;

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

static void
create_graphics(struct data *data)
{
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

        data->graphics.game = fv_game_new(data->image_data,
                                          &data->graphics.shader_data);

        if (data->graphics.game == NULL)
                goto error;

        return;

error:
        destroy_graphics(data);
        data->quit = true;
}

static void
handle_image_data_event(struct data *data,
                        const SDL_UserEvent *event)
{
        switch ((enum fv_image_data_result) event->code) {
        case FV_IMAGE_DATA_SUCCESS:
                create_graphics(data);
                break;

        case FV_IMAGE_DATA_FAIL:
                data->quit = true;
                break;
        }

        fv_image_data_free(data->image_data);
        data->image_data = NULL;
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
                }
                goto handled;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
                handle_key_event(data, &event->key);
                goto handled;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
                handle_mouse_button(data, &event->button);
                goto handled;

        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
                handle_joystick_button(data, &event->jbutton);
                goto handled;

        case SDL_JOYDEVICEADDED:
                handle_joystick_added(data, &event->jdevice);
                goto handled;

        case SDL_JOYDEVICEREMOVED:
                handle_joystick_removed(data, &event->jdevice);
                goto handled;

        case SDL_QUIT:
                data->quit = true;
                goto handled;
        }

        if (event->type == data->image_data_event) {
                handle_image_data_event(data, &event->user);
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
close_joysticks(struct data *data)
{
        SDL_Joystick **joysticks = (SDL_Joystick **) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (SDL_Joystick *);
        int i;

        for (i = 0; i < n_joysticks; i++)
                SDL_JoystickClose(joysticks[i]);

        fv_buffer_destroy(&data->joysticks);
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
paint(struct data *data)
{
        GLbitfield clear_mask = GL_DEPTH_BUFFER_BIT;
        int w, h;
        int i;

        SDL_GetWindowSize(data->window, &w, &h);

        if (w != data->last_fb_width || h != data->last_fb_height) {
                fv_gl.glViewport(0, 0, w, h);
                data->last_fb_width = w;
                data->last_fb_height = h;
                data->viewports_dirty = true;
        }

        fv_logic_update(data->logic, SDL_GetTicks() - data->start_time);

        update_viewports(data);
        update_centers(data);

        if (need_clear(data))
                clear_mask |= GL_COLOR_BUFFER_BIT;

        fv_gl.glClear(clear_mask);

        for (i = 0; i < data->n_viewports; i++) {
                if (data->n_viewports != 1)
                        fv_gl.glViewport(data->players[i].viewport_x,
                                         data->players[i].viewport_y,
                                         data->players[i].viewport_width,
                                         data->players[i].viewport_height);
                fv_game_paint(data->graphics.game,
                              data->players[i].center_x,
                              data->players[i].center_y,
                              data->players[i].viewport_width,
                              data->players[i].viewport_height,
                              data->logic);
        }

        if (data->n_viewports != 1)
                fv_gl.glViewport(0, 0, w, h);

        paint_hud(data, w, h);

        SDL_GL_SwapWindow(data->window);
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

        if (fv_gl.major_version < MIN_GL_MAJOR_VERSION ||
                   (fv_gl.major_version == MIN_GL_MAJOR_VERSION &&
                    fv_gl.minor_version < MIN_GL_MINOR_VERSION)) {
                fv_error_message("GL version %i.%i is required but the driver "
                                 "is reporting:\n"
                                 "Version: %s\n"
                                 "Vendor: %s\n"
                                 "Renderer: %s",
                                 MIN_GL_MAJOR_VERSION,
                                 MIN_GL_MINOR_VERSION,
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

static SDL_GLContext
create_gl_context(SDL_Window *window)
{
        SDL_GLContext context;

        /* First try creating a core context because if we get one it
         * can be more efficient.
         */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            CORE_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            CORE_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);

        context = SDL_GL_CreateContext(window);

        if (context != NULL)
                return context;

        /* Otherwise try a compatibility profile context */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            REQUEST_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            REQUEST_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            FV_GL_PROFILE);

        return SDL_GL_CreateContext(window);
}

static void
iterate_main_loop(struct data *data)
{
        SDL_Event event;

        if (data->graphics.game == NULL) {
                SDL_WaitEvent(&event);
                data->start_time = SDL_GetTicks();
                handle_event(data, &event);
                return;
        }

        if (SDL_PollEvent(&event)) {
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

        configs = glXChooseFBConfig(data->display,
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
        PFNGLXCREATECONTEXTATTRIBSARBPROC fnXCreateContextAttribs;
        GLXContext context;
        const char *extensions;
        static const int attrib_list[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, CORE_GL_MAJOR_VERSION,
                GLX_CONTEXT_MINOR_VERSION_ARB, CORE_GL_MINOR_VERSION,
                GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
                None
        };

        /* Make sure that the display supports the ARB_create_context
         * extension.
         */
        extensions = glXQueryExtensionsString(data->display,
                                              DefaultScreen(data->display));

        if (!fv_gl_check_extension(extensions, "GLX_ARB_create_context")) {
                fv_error_message("GLX_ARB_create_context extension is not "
                                 "supported");
                return NULL;
        }

        fnXCreateContextAttribs =
                (void *) glXGetProcAddress((const GLubyte *)
                                           "glXCreateContextAttribsARB");

        context = fnXCreateContextAttribs(data->display,
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
        int glx_major, glx_minor;

        glXQueryVersion(data->display, &glx_major, &glx_minor);

        if (glx_major < 1 || (glx_major == 1 && glx_minor < 3)) {
                fv_error_message("GLX 1.3 support is required but only "
                                 "%i.%i was found",
                                 glx_major, glx_minor);
                return false;
        }

        root = RootWindow(data->display, 0 /* screen */);

        fb_config = choose_fb_config(data);

        if (fb_config == NULL) {
                fv_error_message("Couldn't get an RGB, double-buffered "
                                 "FB config");
                return false;
        }

        visinfo = glXGetVisualFromFBConfig(data->display, fb_config);

        if (visinfo == NULL) {
                fv_error_message("FB config does not have an associated "
                                 "visual");
                return false;
        }

        data->glx_context = create_context(data, fb_config);

        if (data->glx_context == NULL)
                return false;

        /* window attributes */
        attr.background_pixel = 0;
        attr.border_pixel = 0;
        attr.colormap = XCreateColormap(data->display,
                                        root,
                                        visinfo->visual,
                                        AllocNone);
        attr.event_mask = (StructureNotifyMask | ExposureMask |
                           PointerMotionMask | KeyPressMask);
        mask = CWBorderPixel | CWColormap | CWEventMask;

        data->x_window = XCreateWindow(data->display, root, 0, 0, 800, 600,
                                       0, visinfo->depth, InputOutput,
                                       visinfo->visual, mask, &attr);
        if (!data->x_window) {
                fv_error_message("XCreateWindow failed");
                glXDestroyContext(data->display, data->glx_context);
                return false;
        }

        data->glx_window = glXCreateWindow(data->display,
                                           fb_config,
                                           data->x_window,
                                           NULL /* attrib_list */);

        glXMakeContextCurrent(data->display,
                              data->glx_window,
                              data->glx_window,
                              data->glx_context);

        return true;
}

int
main(int argc, char **argv)
{
        struct data data;
        Uint32 flags;
        int res;
        int ret = EXIT_SUCCESS;
        int i;

        data.is_fullscreen = true;

        memset(&data.graphics, 0, sizeof data.graphics);

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        data.display = XOpenDisplay(NULL);
        if (data.display == NULL) {
                fv_error_message("Error: XOpenDisplay failed");
                ret = EXIT_FAILURE;
                goto out;
        }

        if (!make_window(&data)) {
                ret = EXIT_FAILURE;
                goto out_display;
        }

        res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
        if (res < 0) {
                fv_error_message("Unable to init SDL: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_window;
        }

        fv_buffer_init(&data.joysticks);

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
        if (data.is_fullscreen)
                flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

        /* First try creating a window with multisampling */
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);

        for (i = 0; ; i++) {
                data.window = SDL_CreateWindow("Finvenkisto",
                                               SDL_WINDOWPOS_UNDEFINED,
                                               SDL_WINDOWPOS_UNDEFINED,
                                               800, 600,
                                               flags);
                if (data.window)
                        break;

                if (i == 0) {
                        /* Try again without multisampling */
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
                } else {
                        fv_error_message("Failed to create SDL window: %s",
                                         SDL_GetError());
                        ret = EXIT_FAILURE;
                        goto out_sdl;
                }
        }

        data.gl_context = create_gl_context(data.window);
        if (data.gl_context == NULL) {
                fv_error_message("Failed to create GL context: %s",
                                 SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_sdl_window;
        }

        SDL_GL_MakeCurrent(data.window, data.gl_context);

        fv_gl_init();

        /* SDL seems to happily give you a GL 2 context if you ask for
         * a 3.x core profile but it can't provide one so we have to
         * additionally check that we actually got what we asked
         * for */
        if (!check_gl_version()) {
                ret = EXIT_FAILURE;
                goto out_context;
        }

        SDL_ShowCursor(0);

        data.image_data_event = SDL_RegisterEvents(1);

        data.quit = false;

        data.logic = fv_logic_new();

        data.image_data = fv_image_data_new(data.image_data_event);

        reset_menu_state(&data);

        while (!data.quit)
                iterate_main_loop(&data);

        fv_logic_free(data.logic);

        destroy_graphics(&data);

        if (data.image_data)
                fv_image_data_free(data.image_data);

 out_context:
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_DeleteContext(data.gl_context);
 out_sdl_window:
        SDL_DestroyWindow(data.window);
 out_sdl:
        close_joysticks(&data);
        SDL_Quit();
 out_window:
        glXMakeCurrent(data.display, None, NULL);
        glXDestroyContext(data.display, data.glx_context);
        glXDestroyWindow(data.display, data.glx_window);
        XDestroyWindow(data.display, data.x_window);
 out_display:
        XCloseDisplay(data.display);
 out:
        return ret;
}
