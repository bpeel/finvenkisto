/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014, 2015, 2017 Neil Roberts
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

#ifdef EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
/* On Emscripten you have to request 2.0 to get a 2.0 ES context but
 * the version is reports in GL_VERSION is 1.0 because that is the
 * WebGL version.
 */
#define MIN_GL_MAJOR_VERSION 1
#define MIN_GL_MINOR_VERSION 0
#define REQUEST_GL_MAJOR_VERSION 2
#define REQUEST_GL_MINOR_VERSION 0
#define FV_GL_PROFILE SDL_GL_CONTEXT_PROFILE_ES
#else
#define MIN_GL_MAJOR_VERSION 2
#define MIN_GL_MINOR_VERSION 0
#define REQUEST_GL_MAJOR_VERSION MIN_GL_MAJOR_VERSION
#define REQUEST_GL_MINOR_VERSION MIN_GL_MINOR_VERSION
#define CORE_GL_MAJOR_VERSION 3
#define CORE_GL_MINOR_VERSION 1
#define FV_GL_PROFILE SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
#endif

/* Minimum movement before we consider the joystick axis to be moving.
 * This is 20% of the total.
 */
#define MIN_JOYSTICK_AXIS_MOVEMENT (32767 * 2 / 10)
/* Maximum movement before we consider the joystick to be at full
 * speed. This is 90% of the total.
 */
#define MAX_JOYSTICK_AXIS_MOVEMENT (32767 * 9 / 10)

enum key_code {
        KEY_CODE_UP,
        KEY_CODE_DOWN,
        KEY_CODE_LEFT,
        KEY_CODE_RIGHT,
        KEY_CODE_SHOUT
};

enum control_type {
        CONTROL_TYPE_KEYBOARD,
        CONTROL_TYPE_GAME_CONTROLLER
};

struct keyboard_control_scheme {
        SDL_Keycode up, down, left, right;
        /* Any of these map to the shout button for this scheme */
        SDL_Keycode shout_buttons[8];
};

static const struct keyboard_control_scheme
keyboard_control_schemes[] = {
        { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,
          { SDLK_SPACE, SDLK_LSHIFT, SDLK_RSHIFT, SDLK_LCTRL, SDLK_RCTRL } },
        { SDLK_w, SDLK_s, SDLK_a, SDLK_d,
          { SDLK_q, SDLK_e } },
        { SDLK_i, SDLK_k, SDLK_j, SDLK_l,
          { SDLK_u, SDLK_o, SDLK_SEMICOLON } },
        { SDLK_t, SDLK_g, SDLK_f, SDLK_h,
          { SDLK_r, SDLK_y } },
};

struct player {
        enum control_type control_type;
        /* If the control type is the keyboard then this will be an
         * index into keyboard_control_schemes. Otherwise it is the
         * joystick instance id. */
        int control_id;

        int pressed_keys;

        int16_t x_axis;
        int16_t y_axis;
        float controller_direction;
        float controller_speed;

        int viewport_x, viewport_y;
        int viewport_width, viewport_height;
        float center_x, center_y;
};

enum menu_state {
        MENU_STATE_CHOOSING_N_PLAYERS,
        MENU_STATE_CHOOSING_CONTROLLERS,
        MENU_STATE_PLAYING
};

struct data {
        struct fv_image_data *image_data;
        Uint32 image_data_event;

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

        struct fv_buffer game_controllers;

        struct player players[FV_LOGIC_MAX_PLAYERS];
};

static void
reset_menu_state(struct data *data)
{
        int i;

        data->menu_state = MENU_STATE_CHOOSING_N_PLAYERS;
        data->start_time = SDL_GetTicks();
        data->viewports_dirty = true;
        data->n_viewports = 1;
        data->n_players = 1;

        for (i = 0; i < FV_LOGIC_MAX_PLAYERS; i++) {
                data->players[i].pressed_keys = 0;
                data->players[i].controller_direction = 0.0f;
                data->players[i].controller_speed = 0.0f;
                data->players[i].x_axis = 0;
                data->players[i].y_axis = 0;
        }

        fv_logic_reset(data->logic, 0);
}

#ifndef EMSCRIPTEN
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
#endif /* EMSCRIPTEN */

static void
update_direction(struct data *data,
                 int player_num)
{
        const struct player *player = data->players + player_num;
        float direction;
        float speed = 1.0f;
        int pressed_keys = 0;
        int key_mask;

        pressed_keys = player->pressed_keys;

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
                speed = player->controller_speed;
                direction = player->controller_direction;
                break;
        }

        fv_logic_set_direction(data->logic, player_num, speed, direction);
}

static void
set_key_state(struct data *data,
              int player_num,
              enum key_code key,
              bool state)
{
        struct player *player = data->players + player_num;

        if (key == KEY_CODE_SHOUT) {
                if (data->menu_state == MENU_STATE_PLAYING && state)
                        fv_logic_shout(data->logic, player_num);
        } else if (!!(player->pressed_keys & (1 << key)) != state) {
                if (state)
                        player->pressed_keys |= (1 << key);
                else
                        player->pressed_keys &= ~(1 << key);
                update_direction(data, player_num);
        }
}

static void
start_playing(struct data *data)
{
        data->menu_state = MENU_STATE_PLAYING;
        data->start_time = SDL_GetTicks();
        fv_logic_reset(data->logic, data->n_players);
}

static void
n_players_chosen(struct data *data)
{
        if (data->n_players == 1) {
                start_playing(data);
        } else {
                data->next_player = 0;
                data->menu_state = MENU_STATE_CHOOSING_CONTROLLERS;
        }
        data->viewports_dirty = true;
}

static void
increase_n_players(struct data *data)
{
        data->n_players = data->n_players % FV_LOGIC_MAX_PLAYERS + 1;
}

static void
decrease_n_players(struct data *data)
{
        data->n_players = ((data->n_players + FV_LOGIC_MAX_PLAYERS - 2) %
                           FV_LOGIC_MAX_PLAYERS + 1);
}

static void
handle_choose_n_players_key(struct data *data,
                            const SDL_KeyboardEvent *event)
{
        if (event->state != SDL_PRESSED)
                return;

        switch (event->keysym.sym) {
        case SDLK_w:
        case SDLK_UP:
                decrease_n_players(data);
                break;
        case SDLK_TAB:
        case SDLK_s:
        case SDLK_DOWN:
                increase_n_players(data);
                break;
        case SDLK_RETURN:
        case SDLK_SPACE:
                n_players_chosen(data);
                break;
        }
}

static void
handle_choose_controllers_key(struct data *data,
                              const SDL_KeyboardEvent *event)
{
        SDL_Keycode sym = event->keysym.sym;
        const struct keyboard_control_scheme *scheme;
        int scheme_index, i;

        if (event->state != SDL_PRESSED)
                return;

        /* Check if the key matches any control scheme */

        for (scheme_index = 0;
             scheme_index < FV_N_ELEMENTS(keyboard_control_schemes);
             scheme_index++) {
                scheme = keyboard_control_schemes + scheme_index;

                if (scheme->up == sym ||
                    scheme->down == sym ||
                    scheme->left == sym ||
                    scheme->right == sym)
                        goto found_key;

                for (i = 0; scheme->shout_buttons[i]; i++) {
                        if (scheme->shout_buttons[i] == sym)
                                goto found_key;
                }
        }

        return;

found_key:

        /* Check in any other players have already claimed this scheme */

        for (i = 0; i < data->next_player; i++) {
                if (data->players[i].control_type == CONTROL_TYPE_KEYBOARD &&
                    data->players[i].control_id == scheme_index)
                        return;
        }

        data->players[data->next_player].control_type = CONTROL_TYPE_KEYBOARD;
        data->players[data->next_player].control_id = scheme_index;

        data->next_player++;

        if (data->next_player >= data->n_players)
                start_playing(data);
}

static void
handle_playing_key(struct data *data,
                   const SDL_KeyboardEvent *event)
{
        SDL_Keycode sym = event->keysym.sym;
        const struct keyboard_control_scheme *scheme;
        int scheme_index, player_num;
        bool state = event->state == SDL_PRESSED;
        enum key_code key_code;
        int i;

        for (scheme_index = 0;
             scheme_index < FV_N_ELEMENTS(keyboard_control_schemes);
             scheme_index++) {
                scheme = keyboard_control_schemes + scheme_index;

                if (scheme->up == sym) {
                        key_code = KEY_CODE_UP;
                        goto found_key;
                } else if (scheme->down == sym) {
                        key_code = KEY_CODE_DOWN;
                        goto found_key;
                } else if (scheme->left == sym) {
                        key_code = KEY_CODE_LEFT;
                        goto found_key;
                } else if (scheme->right == sym) {
                        key_code = KEY_CODE_RIGHT;
                        goto found_key;
                }

                for (i = 0; scheme->shout_buttons[i]; i++) {
                        if (scheme->shout_buttons[i] == sym) {
                                key_code = KEY_CODE_SHOUT;
                                goto found_key;
                        }
                }
        }

        return;

found_key:

        /* If there is only one player then all of the control schemes
         * control him/her */
        if (data->n_players == 1) {
                set_key_state(data,
                              0, /* player_num */
                              key_code,
                              state);
                return;
        }

        for (player_num = 0; player_num < data->n_players; player_num++) {
                if (data->players[player_num].control_type ==
                    CONTROL_TYPE_KEYBOARD &&
                    data->players[player_num].control_id == scheme_index) {
                        set_key_state(data,
                                      player_num,
                                      key_code,
                                      state);
                        break;
                }
        }
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

#ifndef EMSCRIPTEN
        case SDLK_F11:
                if (event->state == SDL_PRESSED)
                        toggle_fullscreen(data);
                break;
#endif

        default:
                switch (data->menu_state) {
                case MENU_STATE_CHOOSING_N_PLAYERS:
                        handle_choose_n_players_key(data, event);
                        break;
                case MENU_STATE_CHOOSING_CONTROLLERS:
                        handle_choose_controllers_key(data, event);
                        break;
                case MENU_STATE_PLAYING:
                        handle_playing_key(data, event);
                        break;
                }
                break;
        }
}

static void
handle_choose_n_players_button(struct data *data,
                               const SDL_ControllerButtonEvent *event)
{
        if (event->state != SDL_PRESSED)
                return;

        switch (event->button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                decrease_n_players(data);
                break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_BACK:
                increase_n_players(data);
                break;
        case SDL_CONTROLLER_BUTTON_START:
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_Y:
                n_players_chosen(data);
                break;
        }
}

static void
handle_choose_controllers_button(struct data *data,
                                 const SDL_ControllerButtonEvent *event)
{
        int player_num;

        if (event->state != SDL_PRESSED)
                return;

        /* Check if anyone else is already using this controller */
        for (player_num = 0; player_num < data->next_player; player_num++) {
                if (data->players[player_num].control_type ==
                    CONTROL_TYPE_GAME_CONTROLLER &&
                    data->players[player_num].control_id == event->which)
                        return;
        }

        data->players[data->next_player].control_type =
                CONTROL_TYPE_GAME_CONTROLLER;
        data->players[data->next_player].control_id = event->which;

        data->next_player++;

        if (data->next_player >= data->n_players)
                start_playing(data);
}

static void
handle_playing_button(struct data *data,
                      const SDL_ControllerButtonEvent *event)
{
        bool state = event->state == SDL_PRESSED;
        enum key_code key_code;
        int player_num;

        switch (event->button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                key_code = KEY_CODE_UP;
                break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                key_code = KEY_CODE_DOWN;
                break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                key_code = KEY_CODE_LEFT;
                break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                key_code = KEY_CODE_RIGHT;
                break;
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_Y:
                key_code = KEY_CODE_SHOUT;
                break;
        default:
                return;
        }

        /* If there is only one player then all of the controllers
         * affect him/her */

        if (data->n_players == 1) {
                set_key_state(data,
                              0, /* player_num */
                              key_code,
                              state);
                return;
        }

        /* Check if anyone is using this controller */
        for (player_num = 0; player_num < data->next_player; player_num++) {
                if (data->players[player_num].control_type ==
                    CONTROL_TYPE_GAME_CONTROLLER &&
                    data->players[player_num].control_id == event->which) {
                        set_key_state(data,
                                      player_num,
                                      key_code,
                                      state);
                        break;
                }
        }
}

static void
handle_game_controller_button(struct data *data,
                              const SDL_ControllerButtonEvent *event)
{
        switch (data->menu_state) {
        case MENU_STATE_CHOOSING_N_PLAYERS:
                handle_choose_n_players_button(data, event);
                break;
        case MENU_STATE_CHOOSING_CONTROLLERS:
                handle_choose_controllers_button(data, event);
                break;
        case MENU_STATE_PLAYING:
                handle_playing_button(data, event);
                break;
        }
}

static void
handle_game_controller_axis_motion(struct data *data,
                                   const SDL_ControllerAxisEvent *event)
{
        struct player *player;
        int mag_squared;
        int16_t value = event->value;
        int player_num;

        /* Ignore axes other than 0 and 1 */
        if (event->axis & ~1)
                return;

        if (data->menu_state != MENU_STATE_PLAYING)
                return;

        /* If there is only one player then any controller can be
         * used */
        if (data->n_players == 1) {
                player = data->players;
                player_num = 0;
                goto found_player;
        }

        for (player_num = 0; player_num < data->n_players; player_num++) {
                player = data->players + player_num;

                if (player->control_type == CONTROL_TYPE_GAME_CONTROLLER &&
                    player->control_id == event->which)
                        goto found_player;
        }

        return;

found_player:

        if (value < -INT16_MAX)
                value = -INT16_MAX;

        if (event->axis)
                player->y_axis = -value;
        else
                player->x_axis = value;

        mag_squared = (player->y_axis * (int) player->y_axis +
                       player->x_axis * (int) player->x_axis);

        if (mag_squared <= (MIN_JOYSTICK_AXIS_MOVEMENT *
                            MIN_JOYSTICK_AXIS_MOVEMENT)) {
                player->controller_direction = 0.0f;
                player->controller_speed = 0.0f;
        } else {
                if (mag_squared >= (MAX_JOYSTICK_AXIS_MOVEMENT *
                                    MAX_JOYSTICK_AXIS_MOVEMENT)) {
                        player->controller_speed = 1.0f;
                } else {
                        player->controller_speed =
                                ((sqrtf(mag_squared) -
                                  MIN_JOYSTICK_AXIS_MOVEMENT) /
                                 (MAX_JOYSTICK_AXIS_MOVEMENT -
                                  MIN_JOYSTICK_AXIS_MOVEMENT));
                }
                player->controller_direction = atan2f(player->y_axis,
                                                      player->x_axis);
        }

        update_direction(data, player_num);
}

static void
handle_joystick_added(struct data *data,
                      const SDL_JoyDeviceEvent *event)
{
        SDL_GameController *controller = SDL_GameControllerOpen(event->which);
        SDL_Joystick *joystick, *other_joystick;
        SDL_JoystickID joystick_id, other_joystick_id;
        SDL_GameController **controllers =
                (SDL_GameController **) data->game_controllers.data;
        int n_controllers = (data->game_controllers.length /
                             sizeof (SDL_GameController *));
        int i;

        if (controller == NULL) {
                fprintf(stderr, "failed to open game controller %i: %s\n",
                        event->which,
                        SDL_GetError());
                return;
        }

        joystick = SDL_GameControllerGetJoystick(controller);
        joystick_id = SDL_JoystickInstanceID(joystick);

        /* Check if we already have this controller open */
        for (i = 0; i < n_controllers; i++) {
                other_joystick = SDL_GameControllerGetJoystick(controllers[i]);
                other_joystick_id = SDL_JoystickInstanceID(other_joystick);

                if (joystick_id == other_joystick_id) {
                        SDL_GameControllerClose(controller);
                        return;
                }
        }

        fv_buffer_append(&data->game_controllers,
                         &controller,
                         sizeof controller);
}

static void
handle_joystick_removed(struct data *data,
                        const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick *joystick;
        SDL_JoystickID joystick_id;
        SDL_GameController **controllers =
                (SDL_GameController **) data->game_controllers.data;
        int n_controllers = (data->game_controllers.length /
                             sizeof (SDL_GameController *));
        int i;

        for (i = 0; i < n_controllers; i++) {
                joystick = SDL_GameControllerGetJoystick(controllers[i]);
                joystick_id = SDL_JoystickInstanceID(joystick);

                if (joystick_id == event->which) {
                        SDL_GameControllerClose(controllers[i]);
                        if (i < n_controllers - 1)
                                controllers[i] = controllers[n_controllers - 1];
                        data->game_controllers.length -=
                                sizeof (SDL_GameController *);
                        break;
                }
        }
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

#ifdef EMSCRIPTEN
        emscripten_resume_main_loop();
#endif

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

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
                handle_game_controller_button(data, &event->cbutton);
                goto handled;

        case SDL_CONTROLLERAXISMOTION:
                handle_game_controller_axis_motion(data, &event->caxis);
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
                fv_hud_paint_player_select(data->graphics.hud,
                                           data->n_players,
                                           w, h);
                break;
        case MENU_STATE_CHOOSING_CONTROLLERS:
                fv_hud_paint_controller_select(data->graphics.hud,
                                               w, h,
                                               data->next_player,
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
close_game_controllers(struct data *data)
{
        SDL_GameController **controllers =
                (SDL_GameController **) data->game_controllers.data;
        int n_controllers = (data->game_controllers.length /
                             sizeof (SDL_GameController *));
        int i;

        for (i = 0; i < n_controllers; i++)
                SDL_GameControllerClose(controllers[i]);

        fv_buffer_destroy(&data->game_controllers);
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
#ifndef EMSCRIPTEN
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
#endif /* EMSCRIPTEN */

        /* Otherwise try a compatibility profile context */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            REQUEST_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            REQUEST_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            FV_GL_PROFILE);

        return SDL_GL_CreateContext(window);
}

#ifdef EMSCRIPTEN

static void
emscripten_loop_cb(void *user_data)
{
        struct data *data = user_data;

        paint(data);
}

static int
emscripten_event_filter(void *userdata,
                        SDL_Event *event)
{
        handle_event(userdata, event);

        /* Filter the event */
        return 0;
}

static EM_BOOL
context_lost_cb(int event_type,
                const void *reserved,
                void *user_data)
{
        struct data *data = user_data;

        destroy_graphics(data);

        /* Cancel loading the images */
        if (data->image_data) {
                fv_image_data_free(data->image_data);
                data->image_data = NULL;
        } else {
                emscripten_pause_main_loop();
        }

        return true;
}

static EM_BOOL
context_restored_cb(int event_type,
                    const void *reserved,
                    void *user_data)
{
        struct data *data = user_data;

        /* When the context is lost all of the extension objects that
         * Emscripten created become invalid so it needs to query them
         * again. Ideally it would handle this itself internally. This
         * is probably poking into its internals a bit.
         */
        EM_ASM({
                        var context = GL.currentContext;
                        context.initExtensionsDone = false;
                        GL.initExtensions(context);
                });

        /* Reload the images. This will also reload the graphics when
         * it has finished.
         */
        if (data->image_data == NULL)
                data->image_data = fv_image_data_new(data->image_data_event);

        return true;
}

#else  /* EMSCRIPTEN */

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

#endif /* EMSCRIPTEN */

int
main(int argc, char **argv)
{
        struct data data;
        Uint32 flags;
        int res;
        int ret = EXIT_SUCCESS;
        int i;

#ifdef EMSCRIPTEN
        data.is_fullscreen = false;
#else
        data.is_fullscreen = true;
#endif

        memset(&data.graphics, 0, sizeof data.graphics);

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        res = SDL_Init(SDL_INIT_VIDEO |
                       SDL_INIT_JOYSTICK |
                       SDL_INIT_GAMECONTROLLER);
        if (res < 0) {
                fv_error_message("Unable to init SDL: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out;
        }

        fv_buffer_init(&data.game_controllers);

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
                goto out_window;
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

#ifdef EMSCRIPTEN

        emscripten_set_webglcontextlost_callback("canvas",
                                                 &data,
                                                 false /* useCapture */,
                                                 context_lost_cb);
        emscripten_set_webglcontextrestored_callback("canvas",
                                                     &data,
                                                     false /* useCapture */,
                                                     context_restored_cb);

        SDL_SetEventFilter(emscripten_event_filter, &data);

        emscripten_set_main_loop_arg(emscripten_loop_cb,
                                     &data,
                                     0, /* fps (use browser's choice) */
                                     false /* simulate infinite loop */);
        emscripten_pause_main_loop();
        emscripten_exit_with_live_runtime();

#else
        while (!data.quit)
                iterate_main_loop(&data);
#endif

        fv_logic_free(data.logic);

        destroy_graphics(&data);

        if (data.image_data)
                fv_image_data_free(data.image_data);

 out_context:
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_DeleteContext(data.gl_context);
 out_window:
        SDL_DestroyWindow(data.window);
 out_sdl:
        close_game_controllers(&data);
        SDL_Quit();
 out:
        return ret;
}
