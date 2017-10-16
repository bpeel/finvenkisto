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

#include "config.h"

#include <SDL.h>
#include <stdio.h>

#include "fv-input.h"
#include "fv-buffer.h"

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
};

struct fv_input {
        struct fv_logic *logic;

        int n_players;
        int next_player;

        struct fv_buffer game_controllers;

        struct player players[FV_LOGIC_MAX_PLAYERS];

        enum fv_input_state state;

        fv_input_state_changed_cb state_changed_cb;
        void *state_changed_cb_user_data;
};

static void
set_state(struct fv_input *input,
          enum fv_input_state state)
{
        input->state = state;

        if (input->state_changed_cb)
                input->state_changed_cb(input->state_changed_cb_user_data);
}

void
fv_input_reset(struct fv_input *input)
{
        int i;

        input->next_player = 0;
        input->n_players = 1;

        for (i = 0; i < FV_LOGIC_MAX_PLAYERS; i++) {
                input->players[i].pressed_keys = 0;
                input->players[i].controller_direction = 0.0f;
                input->players[i].controller_speed = 0.0f;
                input->players[i].x_axis = 0;
                input->players[i].y_axis = 0;
        }

        input->state = FV_INPUT_STATE_CHOOSING_N_PLAYERS;
}

struct fv_input *
fv_input_new(struct fv_logic *logic)
{
        struct fv_input *input = fv_alloc(sizeof *input);

        input->logic = logic;
        input->state_changed_cb = NULL;

        fv_input_reset(input);

        fv_buffer_init(&input->game_controllers);

        return input;
}

void
fv_input_set_state_changed_cb(struct fv_input *input,
                              fv_input_state_changed_cb cb,
                              void *user_data)
{
        input->state_changed_cb = cb;
        input->state_changed_cb_user_data = user_data;
}

enum fv_input_state
fv_input_get_state(struct fv_input *input)
{
        return input->state;
}

int
fv_input_get_n_players(struct fv_input *input)
{
        return input->n_players;
}

int
fv_input_get_next_player(struct fv_input *input)
{
        return input->next_player;
}

static void
update_direction(struct fv_input *input,
                 int player_num)
{
        const struct player *player = input->players + player_num;
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

        fv_logic_set_direction(input->logic, player_num, speed, direction);
}

static void
set_key_state(struct fv_input *input,
              int player_num,
              enum key_code key,
              bool state)
{
        struct player *player = input->players + player_num;

        if (key == KEY_CODE_SHOUT) {
                if (input->state == FV_INPUT_STATE_PLAYING && state)
                        fv_logic_shout(input->logic, player_num);
        } else if (!!(player->pressed_keys & (1 << key)) != state) {
                if (state)
                        player->pressed_keys |= (1 << key);
                else
                        player->pressed_keys &= ~(1 << key);
                update_direction(input, player_num);
        }
}

static void
start_playing(struct fv_input *input)
{
        set_state(input, FV_INPUT_STATE_PLAYING);
}

static void
n_players_chosen(struct fv_input *input)
{
        if (input->n_players == 1) {
                start_playing(input);
        } else {
                input->next_player = 0;
                set_state(input, FV_INPUT_STATE_CHOOSING_CONTROLLERS);
        }
}

static void
increase_n_players(struct fv_input *input)
{
        input->n_players = input->n_players % FV_LOGIC_MAX_PLAYERS + 1;
}

static void
decrease_n_players(struct fv_input *input)
{
        input->n_players = ((input->n_players + FV_LOGIC_MAX_PLAYERS - 2) %
                           FV_LOGIC_MAX_PLAYERS + 1);
}

static bool
handle_choose_n_players_key(struct fv_input *input,
                            const SDL_KeyboardEvent *event)
{
        if (event->state != SDL_PRESSED)
                return false;

        switch (event->keysym.sym) {
        case SDLK_w:
        case SDLK_UP:
                decrease_n_players(input);
                return true;
        case SDLK_TAB:
        case SDLK_s:
        case SDLK_DOWN:
                increase_n_players(input);
                return true;
        case SDLK_RETURN:
        case SDLK_SPACE:
                n_players_chosen(input);
                return true;
        }

        return false;
}

static bool
handle_choose_controllers_key(struct fv_input *input,
                              const SDL_KeyboardEvent *event)
{
        SDL_Keycode sym = event->keysym.sym;
        const struct keyboard_control_scheme *scheme;
        int scheme_index, i;

        if (event->state != SDL_PRESSED)
                return false;

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

        return false;

found_key:

        /* Check in any other players have already claimed this scheme */

        for (i = 0; i < input->next_player; i++) {
                if (input->players[i].control_type == CONTROL_TYPE_KEYBOARD &&
                    input->players[i].control_id == scheme_index)
                        return true;
        }

        input->players[input->next_player].control_type = CONTROL_TYPE_KEYBOARD;
        input->players[input->next_player].control_id = scheme_index;

        input->next_player++;

        if (input->next_player >= input->n_players)
                start_playing(input);

        return true;
}

static bool
handle_playing_key(struct fv_input *input,
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

        return false;

found_key:

        /* If there is only one player then all of the control schemes
         * control him/her */
        if (input->n_players == 1) {
                set_key_state(input,
                              0, /* player_num */
                              key_code,
                              state);
                return true;
        }

        for (player_num = 0; player_num < input->n_players; player_num++) {
                if (input->players[player_num].control_type ==
                    CONTROL_TYPE_KEYBOARD &&
                    input->players[player_num].control_id == scheme_index) {
                        set_key_state(input,
                                      player_num,
                                      key_code,
                                      state);
                        return true;
                }
        }

        return false;
}

static bool
handle_key_event(struct fv_input *input,
                 const SDL_KeyboardEvent *event)
{
        switch (input->state) {
        case FV_INPUT_STATE_CHOOSING_N_PLAYERS:
                return handle_choose_n_players_key(input, event);
        case FV_INPUT_STATE_CHOOSING_CONTROLLERS:
                return handle_choose_controllers_key(input, event);
        case FV_INPUT_STATE_PLAYING:
                return handle_playing_key(input, event);
        }

        return false;
}

static void
close_game_controllers(struct fv_input *input)
{
        SDL_GameController **controllers =
                (SDL_GameController **) input->game_controllers.data;
        int n_controllers = (input->game_controllers.length /
                             sizeof (SDL_GameController *));
        int i;

        for (i = 0; i < n_controllers; i++)
                SDL_GameControllerClose(controllers[i]);

        fv_buffer_destroy(&input->game_controllers);
}

static bool
handle_choose_n_players_button(struct fv_input *input,
                               const SDL_ControllerButtonEvent *event)
{
        if (event->state != SDL_PRESSED)
                return false;

        switch (event->button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                decrease_n_players(input);
                return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_BACK:
                increase_n_players(input);
                return true;
        case SDL_CONTROLLER_BUTTON_START:
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_Y:
                n_players_chosen(input);
                return true;
        }

        return false;
}

static bool
handle_choose_controllers_button(struct fv_input *input,
                                 const SDL_ControllerButtonEvent *event)
{
        int player_num;

        if (event->state != SDL_PRESSED)
                return false;

        /* Check if anyone else is already using this controller */
        for (player_num = 0; player_num < input->next_player; player_num++) {
                if (input->players[player_num].control_type ==
                    CONTROL_TYPE_GAME_CONTROLLER &&
                    input->players[player_num].control_id == event->which)
                        return false;
        }

        input->players[input->next_player].control_type =
                CONTROL_TYPE_GAME_CONTROLLER;
        input->players[input->next_player].control_id = event->which;

        input->next_player++;

        if (input->next_player >= input->n_players)
                start_playing(input);

        return true;
}

static bool
handle_playing_button(struct fv_input *input,
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
                return false;
        }

        /* If there is only one player then all of the controllers
         * affect him/her */

        if (input->n_players == 1) {
                set_key_state(input,
                              0, /* player_num */
                              key_code,
                              state);
                return true;
        }

        /* Check if anyone is using this controller */
        for (player_num = 0; player_num < input->next_player; player_num++) {
                if (input->players[player_num].control_type ==
                    CONTROL_TYPE_GAME_CONTROLLER &&
                    input->players[player_num].control_id == event->which) {
                        set_key_state(input,
                                      player_num,
                                      key_code,
                                      state);
                        return true;
                }
        }

        return false;
}

static bool
handle_game_controller_button(struct fv_input *input,
                              const SDL_ControllerButtonEvent *event)
{
        switch (input->state) {
        case FV_INPUT_STATE_CHOOSING_N_PLAYERS:
                return handle_choose_n_players_button(input, event);
        case FV_INPUT_STATE_CHOOSING_CONTROLLERS:
                return handle_choose_controllers_button(input, event);
        case FV_INPUT_STATE_PLAYING:
                return handle_playing_button(input, event);
        }

        return false;
}

static bool
handle_game_controller_axis_motion(struct fv_input *input,
                                   const SDL_ControllerAxisEvent *event)
{
        struct player *player;
        int mag_squared;
        int16_t value = event->value;
        int player_num;

        /* Ignore axes other than 0 and 1 */
        if (event->axis & ~1)
                return false;

        if (input->state != FV_INPUT_STATE_PLAYING)
                return false;

        /* If there is only one player then any controller can be
         * used */
        if (input->n_players == 1) {
                player = input->players;
                player_num = 0;
                goto found_player;
        }

        for (player_num = 0; player_num < input->n_players; player_num++) {
                player = input->players + player_num;

                if (player->control_type == CONTROL_TYPE_GAME_CONTROLLER &&
                    player->control_id == event->which)
                        goto found_player;
        }

        return false;

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

        update_direction(input, player_num);

        return true;
}

static bool
handle_joystick_added(struct fv_input *input,
                      const SDL_JoyDeviceEvent *event)
{
        SDL_GameController *controller = SDL_GameControllerOpen(event->which);
        SDL_Joystick *joystick, *other_joystick;
        SDL_JoystickID joystick_id, other_joystick_id;
        SDL_GameController **controllers =
                (SDL_GameController **) input->game_controllers.data;
        int n_controllers = (input->game_controllers.length /
                             sizeof (SDL_GameController *));
        int i;

        if (controller == NULL) {
                fprintf(stderr, "failed to open game controller %i: %s\n",
                        event->which,
                        SDL_GetError());
                return true;
        }

        joystick = SDL_GameControllerGetJoystick(controller);
        joystick_id = SDL_JoystickInstanceID(joystick);

        /* Check if we already have this controller open */
        for (i = 0; i < n_controllers; i++) {
                other_joystick = SDL_GameControllerGetJoystick(controllers[i]);
                other_joystick_id = SDL_JoystickInstanceID(other_joystick);

                if (joystick_id == other_joystick_id) {
                        SDL_GameControllerClose(controller);
                        return true;
                }
        }

        fv_buffer_append(&input->game_controllers,
                         &controller,
                         sizeof controller);

        return true;
}

static bool
handle_joystick_removed(struct fv_input *input,
                        const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick *joystick;
        SDL_JoystickID joystick_id;
        SDL_GameController **controllers =
                (SDL_GameController **) input->game_controllers.data;
        int n_controllers = (input->game_controllers.length /
                             sizeof (SDL_GameController *));
        int i;

        for (i = 0; i < n_controllers; i++) {
                joystick = SDL_GameControllerGetJoystick(controllers[i]);
                joystick_id = SDL_JoystickInstanceID(joystick);

                if (joystick_id == event->which) {
                        SDL_GameControllerClose(controllers[i]);
                        if (i < n_controllers - 1)
                                controllers[i] = controllers[n_controllers - 1];
                        input->game_controllers.length -=
                                sizeof (SDL_GameController *);
                        break;
                }
        }

        return true;
}

bool
fv_input_handle_event(struct fv_input *input,
                      const SDL_Event *event)
{
        switch (event->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
                return handle_key_event(input, &event->key);

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
                return handle_game_controller_button(input, &event->cbutton);

        case SDL_CONTROLLERAXISMOTION:
                return handle_game_controller_axis_motion(input, &event->caxis);

        case SDL_JOYDEVICEADDED:
                return handle_joystick_added(input, &event->jdevice);

        case SDL_JOYDEVICEREMOVED:
                return handle_joystick_removed(input, &event->jdevice);
        }

        return false;
}

void
fv_input_free(struct fv_input *input)
{
        close_game_controllers(input);
        fv_free(input);
}
