/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015 Neil Roberts
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
#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include "fv-image-data.h"
#include "fv-data.h"
#include "fv-util.h"
#include "fv-error-message.h"
#include "fv-gl.h"

#define STB_IMAGE_IMPLEMENTATION 1
#include "stb_image.h"

struct image_details {
        int width, height;
        GLenum format, type;
        uint8_t *pixels;
};

static const char *
image_filenames[] = {
#include "data/fv-image-data-files.h"
};

struct fv_image_data {
        bool loaded;
        struct image_details images[FV_N_ELEMENTS(image_filenames)];
};

static uint8_t *
load_image(const char *name,
           int *width,
           int *height,
           GLenum *format,
           GLenum *type)
{
        char *filename = fv_data_get_filename(name);
        int components;
        uint8_t *data;

        if (filename == NULL) {
                fv_error_message("Failed to get filename for %s", name);
                return NULL;
        }

        data = stbi_load(filename,
                         width, height,
                         &components,
                         0 /* components */);

        if (data == NULL) {
                fv_error_message("%s: %s",
                                 filename,
                                 stbi_failure_reason());
                fv_free(filename);
                return NULL;
        }

        fv_free(filename);

        switch (components) {
        case 3:
                *format = GL_RGB;
                break;
        case 4:
                *format = GL_RGBA;
                break;
        default:
                *format = GL_ALPHA;
                break;
        }

        *type = GL_UNSIGNED_BYTE;

        return data;
}

#ifdef EMSCRIPTEN
void
send_result(uint32_t loaded_event,
            enum fv_image_data_result result);

EMSCRIPTEN_KEEPALIVE void
#else
static void
#endif
send_result(uint32_t loaded_event,
            enum fv_image_data_result result)
{
        SDL_Event event;

        event.type = loaded_event;
        event.user.code = result;

        SDL_PushEvent(&event);
}

struct fv_image_data *
fv_image_data_new(uint32_t loaded_event)
{
        struct fv_image_data *data;
        struct image_details *image;
        enum fv_image_data_result result;
        int i;

        data = fv_alloc(sizeof *data);

        for (i = 0; i < FV_N_ELEMENTS(data->images); i++) {
                image = data->images + i;
                image->pixels = load_image(image_filenames[i],
                                           &image->width,
                                           &image->height,
                                           &image->format,
                                           &image->type);

                if (image->pixels == NULL) {
                        for (; i >= 0; --i)
                                fv_free(data->images[i].pixels);
                        data->loaded = false;
                        result = FV_IMAGE_DATA_FAIL;
                        goto done;
                }
        }

        data->loaded = true;
        result = FV_IMAGE_DATA_SUCCESS;

done:
#ifdef EMSCRIPTEN
        EM_ASM_({
                        var cb = function() { _send_result($0, $1) };
                        window.setTimeout(cb, 0);
                }, loaded_event, result);
#else
        send_result(loaded_event, result);
#endif

        return data;
}

void
fv_image_data_get_size(struct fv_image_data *data,
                       enum fv_image_data_image image,
                       int *width,
                       int *height)
{
        assert(data->loaded);

        *width = data->images[image].width;
        *height = data->images[image].height;
}

void
fv_image_data_set_2d(struct fv_image_data *data,
                     GLenum target,
                     GLint level,
                     GLint internal_format,
                     enum fv_image_data_image image)
{
        const struct image_details *img = data->images + image;
        assert(data->loaded);

        fv_gl.glTexImage2D(target,
                           level,
                           internal_format,
                           img->width, img->height,
                           0, /* border */
                           img->format, img->type,
                           img->pixels);
}

void
fv_image_data_set_sub_2d(struct fv_image_data *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset,
                         enum fv_image_data_image image)
{
        const struct image_details *img = data->images + image;
        assert(data->loaded);

        fv_gl.glTexSubImage2D(target,
                              level,
                              x_offset, y_offset,
                              img->width, img->height,
                              img->format, img->type,
                              img->pixels);
}

void
fv_image_data_set_sub_3d(struct fv_image_data *data,
                         GLenum target,
                         GLint level,
                         GLint x_offset, GLint y_offset, GLint z_offset,
                         enum fv_image_data_image image)
{
        const struct image_details *img = data->images + image;
        assert(data->loaded);

        fv_gl.glTexSubImage3D(target,
                              level,
                              x_offset, y_offset, z_offset,
                              img->width, img->height,
                              1, /* depth */
                              img->format, img->type,
                              img->pixels);
}

void
fv_image_data_free(struct fv_image_data *data)
{
        int i;

        if (data->loaded) {
                for (i = 0; i < FV_N_ELEMENTS(data->images); i++)
                        fv_free(data->images[i].pixels);
        }

        fv_free(data);
}
