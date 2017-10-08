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
#include "fv-map.h"
#include "fv-error-message.h"
#include "fv-input.h"

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

struct viewport {
        int x, y;
        int width, height;
        float center_x, center_y;
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

        struct fv_input *input;

        struct viewport viewports[FV_LOGIC_MAX_PLAYERS];
};

static void
reset_menu_state(struct data *data)
{
        data->start_time = SDL_GetTicks();
        data->viewports_dirty = true;
        data->n_viewports = 1;

        fv_input_reset(data->input);
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

#ifndef EMSCRIPTEN
        case SDLK_F11:
                if (event->state == SDL_PRESSED) {
                        toggle_fullscreen(data);
                        return true;
                }
                break;
#endif
        }

        return false;
}

static void
input_state_changed_cb(void *user_data)
{
        struct data *data = user_data;

        if (fv_input_get_state(data->input) == FV_INPUT_STATE_PLAYING) {
                data->start_time = SDL_GetTicks();
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
                if (handle_key_event(data, &event->key))
                        goto handled;
                break;

        case SDL_QUIT:
                data->quit = true;
                goto handled;
        }

        if (event->type == data->image_data_event) {
                handle_image_data_event(data, &event->user);
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
                                           n_players,
                                           w, h);
                break;
        case FV_INPUT_STATE_CHOOSING_CONTROLLERS:
                fv_hud_paint_controller_select(data->graphics.hud,
                                               w, h,
                                               next_player,
                                               n_players);
                break;
        case FV_INPUT_STATE_PLAYING:
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

        if (fv_input_get_state(data->input) ==
            FV_INPUT_STATE_CHOOSING_N_PLAYERS)
                data->n_viewports = 1;
        else
                data->n_viewports = fv_input_get_n_players(data->input);

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
                data->viewports[i].x = i % 2 * viewport_width;
                data->viewports[i].y = (vertical_divisions - 1 -
                                        i / 2) * viewport_height;
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
                        fv_gl.glViewport(data->viewports[i].x,
                                         data->viewports[i].y,
                                         data->viewports[i].width,
                                         data->viewports[i].height);
                fv_game_paint(data->graphics.game,
                              data->viewports[i].center_x,
                              data->viewports[i].center_y,
                              data->viewports[i].width,
                              data->viewports[i].height,
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
        SDL_Event event;

        while (SDL_PollEvent(&event))
                handle_event(data, &event);

        paint(data);
}

static int
emscripten_event_filter(void *user_data,
                        SDL_Event *event)
{
        struct data *data = user_data;

        if (event->type == data->image_data_event) {
                handle_event(data, event);

                /* Filter the event */
                return 0;
        }

        return 1;
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
        data.input = fv_input_new(data.logic);
        fv_input_set_state_changed_cb(data.input,
                                      input_state_changed_cb,
                                      &data);

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

        fv_input_free(data.input);
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
        SDL_Quit();
 out:
        return ret;
}
