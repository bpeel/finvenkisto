/*
 * Finvenkisto
 *
 * Copyright (C) 2015 Neil Roberts
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
#include <emscripten.h>
#include <assert.h>

#include "fv-image-data.h"
#include "fv-data.h"
#include "fv-util.h"
#include "fv-error-message.h"
#include "fv-gl.h"

struct fv_image_data {
        uint32_t loaded_event;
        bool result_sent;
};

static const char *
fv_image_data_files[] = {
#include "data/fv-image-data-files.h"
        NULL
};

static void
send_result(struct fv_image_data *data,
            enum fv_image_data_result result)
{
        SDL_Event event;

        if (data->result_sent)
                return;

        data->result_sent = true;

        EM_ASM({
                        var i;
                        for (i = 0; i < Module.images.length; i++) {
                                var img = Module.images[i];
                                img.onload = undefined;
                                img.onerror = undefined;
                        }
                });

        event.type = data->loaded_event;
        event.user.code = result;

        SDL_PushEvent(&event);
}

EMSCRIPTEN_KEEPALIVE void
fv_image_data_send_success(struct fv_image_data *data)
{
        send_result(data, FV_IMAGE_DATA_SUCCESS);
}

EMSCRIPTEN_KEEPALIVE void
fv_image_data_send_fail(struct fv_image_data *data)
{
        fv_error_message("There was an error loading an image");
        send_result(data, FV_IMAGE_DATA_FAIL);
}

struct fv_image_data *
fv_image_data_new(uint32_t loaded_event)
{
        struct fv_image_data *data = fv_alloc(sizeof *data);

        data->loaded_event = loaded_event;
        data->result_sent = false;

        EM_ASM_({
                        var image_name;
                        var loaded_count = 0;
                        var i;

                        function load_cb()
                        {
                                loaded_count++;
                                if (loaded_count >= Module.images.length)
                                        _fv_image_data_send_success($1);
                        };

                        function error_cb()
                        {
                                _fv_image_data_send_fail($1);
                        };

                        Module.images = [];

                        for (i = 0; (image_name = HEAP32[($0 >> 2) + i]); i++) {
                                var img = document.createElement("img");
                                img.onload = load_cb;
                                img.onerror = error_cb;
                                img.src = "data/" +
                                        Module.UTF8ToString(image_name);
                                Module.images.push(img);
                        }
                }, fv_image_data_files, data);

        return data;
}

void
fv_image_data_get_size(struct fv_image_data *data,
                       enum fv_image_data_image image,
                       int *width,
                       int *height)
{
        EM_ASM_({
                        var img = Module.images[$0];
                        HEAP32[$1 >> 2] = img.width;
                        HEAP32[$2 >> 2] = img.height;
                }, image, width, height);
}

void
fv_image_data_set_2d(struct fv_image_data *data,
                     GLenum target,
                     GLint level,
                     GLint internal_format,
                     enum fv_image_data_image image)
{
        EM_ASM_({
                        GLctx.texImage2D($0, /* target */
                                         $1, /* level */
                                         $2, /* internalFormat */
                                         $2, /* format */
                                         GLctx.UNSIGNED_BYTE,
                                         Module.images[$3]);
                }, target, level, internal_format, image);
}

void
fv_image_data_set_sub_2d(struct fv_image_data *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset,
                         enum fv_image_data_image image)
{
        EM_ASM_({
                        GLctx.texSubImage2D($0, /* target */
                                            $1, /* level */
                                            $2, $3, /* x/y_offset */
                                            GLctx.RGB,
                                            GLctx.UNSIGNED_BYTE,
                                            Module.images[$4]);
                }, target, level, x_offset, y_offset, image);
}

void
fv_image_data_set_sub_3d(struct fv_image_data *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset, GLint z_offset,
                         enum fv_image_data_image image)
{
        assert(!"3D texturing not available in WebGL");
}

void
fv_image_data_free(struct fv_image_data *data)
{
        EM_ASM({
                        var i;
                        for (i = 0; i < Module.images.length; i++) {
                                var img = Module.images[i];
                                img.onload = undefined;
                                img.onerror = undefined;
                        }
                        Module.images = undefined;
                });

        fv_free(data);
}
