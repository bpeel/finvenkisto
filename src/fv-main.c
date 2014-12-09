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

#include <epoxy/gl.h>
#include <stdio.h>
#include <SDL.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-shader-data.h"

struct data {
        struct fv_shader_data shader_data;
        struct fv_game *game;
        struct fv_logic *logic;

        SDL_Window *window;
        int last_fb_width, last_fb_height;
        SDL_GLContext gl_context;

        bool quit;
        bool is_fullscreen;
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
handle_key_event(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        switch (event->keysym.sym) {
        case SDLK_ESCAPE:
                if (event->state == SDL_PRESSED)
                        data->quit = true;
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
                glViewport(0, 0, w, h);
                data->last_fb_width = w;
                data->last_fb_height = h;
        }

        glClear(GL_DEPTH_BUFFER_BIT);

        fv_logic_update(data->logic, SDL_GetTicks());
        fv_game_paint(data->game, w, h, data->logic);

        SDL_GL_SwapWindow(data->window);
}

int
main(int argc, char **argv)
{
        struct data data;
        SDL_Event event;
        int res;
        int ret;

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
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);

        data.window = SDL_CreateWindow("Finvenkisto",
                                       SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       640, 480, SDL_WINDOW_OPENGL);

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

        SDL_ShowCursor(0);

        /* All of the painting functions expect to have the default
         * OpenGL state plus the following modifications */

        /* ... */

        /* The current program, vertex array and bound textures are
         * not expected to be reset back to zero */

        data.quit = false;

        data.is_fullscreen = false;

        data.last_fb_width = data.last_fb_height = 0;

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
