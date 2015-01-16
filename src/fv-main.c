/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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
#include "fv-shader-data.h"
#include "fv-gl.h"

enum direction_key {
        DIRECTION_KEY_UP = (1 << 0),
        DIRECTION_KEY_DOWN = (1 << 1),
        DIRECTION_KEY_LEFT = (1 << 2),
        DIRECTION_KEY_RIGHT = (1 << 3)
};

struct data {
        struct fv_shader_data shader_data;
        struct fv_game *game;
        struct fv_logic *logic;

        SDL_Window *window;
        int last_fb_width, last_fb_height;
        SDL_GLContext gl_context;

        bool quit;
        bool is_fullscreen;

        enum direction_key direction_keys;
};

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
update_direction(struct data *data)
{
        float direction;
        bool moving = true;
        enum direction_key key, key_mask;

        key_mask = data->direction_keys;
        /* Cancel out directions where opposing keys are pressed */
        key_mask = ((key_mask & 10) >> 1) ^ (key_mask & 5);
        key_mask |= key_mask << 1;
        key = data->direction_keys & key_mask;

        switch ((int) key) {
        case DIRECTION_KEY_UP:
                direction = M_PI / 2.0f;
                break;
        case DIRECTION_KEY_UP | DIRECTION_KEY_LEFT:
                direction = M_PI * 3.0f / 4.0f;
                break;
        case DIRECTION_KEY_UP | DIRECTION_KEY_RIGHT:
                direction = M_PI / 4.0f;
                break;
        case DIRECTION_KEY_DOWN:
                direction = -M_PI / 2.0f;
                break;
        case DIRECTION_KEY_DOWN | DIRECTION_KEY_LEFT:
                direction = -M_PI * 3.0f / 4.0f;
                break;
        case DIRECTION_KEY_DOWN | DIRECTION_KEY_RIGHT:
                direction = -M_PI / 4.0f;
                break;
        case DIRECTION_KEY_LEFT:
                direction = M_PI;
                break;
        case DIRECTION_KEY_RIGHT:
                direction = 0.0f;
                break;
        default:
                moving = false;
                direction = 0.0f;
                break;
        }

        fv_logic_set_direction(data->logic, moving, direction);
}

static void
handle_key_event(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        switch (event->keysym.sym) {
        case SDLK_ESCAPE:
                if (event->state == SDL_PRESSED)
                        data->quit = true;
                break;

        case SDLK_UP:
                if (event->state == SDL_PRESSED)
                        data->direction_keys |= DIRECTION_KEY_UP;
                else
                        data->direction_keys &= ~DIRECTION_KEY_UP;
                update_direction(data);
                break;

        case SDLK_DOWN:
                if (event->state == SDL_PRESSED)
                        data->direction_keys |= DIRECTION_KEY_DOWN;
                else
                        data->direction_keys &= ~DIRECTION_KEY_DOWN;
                update_direction(data);
                break;

        case SDLK_LEFT:
                if (event->state == SDL_PRESSED)
                        data->direction_keys |= DIRECTION_KEY_LEFT;
                else
                        data->direction_keys &= ~DIRECTION_KEY_LEFT;
                update_direction(data);
                break;

        case SDLK_RIGHT:
                if (event->state == SDL_PRESSED)
                        data->direction_keys |= DIRECTION_KEY_RIGHT;
                else
                        data->direction_keys &= ~DIRECTION_KEY_RIGHT;
                update_direction(data);
                break;

        case SDLK_F11:
                if (event->state == SDL_PRESSED)
                        toggle_fullscreen(data);
                break;
        }
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
                break;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
                handle_key_event(data, &event->key);
                break;

        case SDL_QUIT:
                data->quit = true;
                break;
        }
}

static void
paint(struct data *data)
{
        int w, h;

        SDL_GetWindowSize(data->window, &w, &h);

        if (w != data->last_fb_width || h != data->last_fb_height) {
                fv_gl.glViewport(0, 0, w, h);
                data->last_fb_width = w;
                data->last_fb_height = h;
        }

        fv_gl.glClear(GL_DEPTH_BUFFER_BIT);

        fv_logic_update(data->logic, SDL_GetTicks());
        fv_game_paint(data->game, w, h, data->logic);

        SDL_GL_SwapWindow(data->window);
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

FV_NULL_TERMINATED
static bool
check_extensions(const char *extension, ...)
{
        bool missing_extension = false;
        va_list ap;

        va_start(ap, extension);

        do {
                if (!SDL_GL_ExtensionSupported(extension)) {
                        if (!missing_extension) {
                                missing_extension = true;
                                fprintf(stderr,
                                        "The GL implementation does not the "
                                        "support the following required "
                                        "extensions:\n");
                        }
                        fprintf(stderr, "%s\n", extension);
                }
        } while ((extension = va_arg(ap, const char *)));

        va_end(ap);

        return !missing_extension;
}

int
main(int argc, char **argv)
{
        struct data data;
        SDL_Event event;
        Uint32 flags;
        int res;
        int ret;

        data.is_fullscreen = true;

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        res = SDL_Init(SDL_INIT_VIDEO);
        if (res < 0) {
                fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out;
        }

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);

        flags = SDL_WINDOW_OPENGL;
        if (data.is_fullscreen)
                flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

        data.window = SDL_CreateWindow("Finvenkisto",
                                       SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       800, 600,
                                       flags);

        if (data.window == NULL) {
                fprintf(stderr,
                        "Failed to create SDL window: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_sdl;
        }

        data.gl_context = SDL_GL_CreateContext(data.window);
        if (data.gl_context == NULL) {
                fprintf(stderr,
                        "Failed to create GL context: %s\n",
                        SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_window;
        }

        SDL_GL_MakeCurrent(data.window, data.gl_context);

        if (!check_extensions("GL_ARB_instanced_arrays",
                              "GL_ARB_explicit_attrib_location",
                              NULL)) {
                ret = EXIT_FAILURE;
                goto out_context;
        }

        SDL_ShowCursor(0);

        fv_gl_init();

        /* All of the painting functions expect to have the default
         * OpenGL state plus the following modifications */

        fv_gl.glEnable(GL_CULL_FACE);

        /* The current program, vertex array, array buffer and bound
         * textures are not expected to be reset back to zero */

        data.quit = false;

        data.last_fb_width = data.last_fb_height = 0;

        data.direction_keys = 0;

        if (!fv_shader_data_init(&data.shader_data)) {
                ret = EXIT_FAILURE;
                goto out_context;
        }

        data.game = fv_game_new(&data.shader_data);

        if (data.game == NULL) {
                ret = EXIT_FAILURE;
                goto out_shader_data;
        }

        data.logic = fv_logic_new();

        while (!data.quit) {
                if (!SDL_PollEvent(&event))
                        paint(&data);

                handle_event(&data, &event);
        }

        fv_logic_free(data.logic);

        fv_game_free(data.game);
 out_shader_data:
        fv_shader_data_destroy(&data.shader_data);
 out_context:
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_DeleteContext(data.gl_context);
 out_window:
        SDL_DestroyWindow(data.window);
 out_sdl:
        SDL_Quit();
 out:
        return ret;
}
