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

#include <assert.h>
#include <stdarg.h>

#include "fv-hud.h"
#include "fv-shader-data.h"
#include "fv-util.h"
#include "fv-image.h"
#include "fv-logic.h"
#include "fv-gl.h"

struct fv_hud_vertex {
        float x, y;
        float s, t;
};

struct fv_hud {
        GLuint tex;
        int tex_width, tex_height;

        GLuint program;

        GLuint vertex_buffer;
        GLuint element_buffer;
        GLuint array;

        int n_rectangles;
        struct fv_hud_vertex *vertex;
        int screen_width, screen_height;
};

struct fv_hud_image {
        int x, y, w, h;
};

#include "data/hud-layout.h"

static const struct fv_hud_image *
fv_hud_key_images[] = {
        &fv_hud_image_up,
        &fv_hud_image_down,
        &fv_hud_image_left,
        &fv_hud_image_right
};

#define FV_HUD_MAX_RECTANGLES 16

struct fv_hud *
fv_hud_new(struct fv_shader_data *shader_data)
{
        struct fv_hud *hud;
        int image_width, image_height;
        uint8_t *image;
        uint8_t *elements;
        GLuint tex_location;
        int i;

        image = fv_image_load("hud.png", &image_width, &image_height, 4);

        if (image == NULL)
                return NULL;

        hud = fv_alloc(sizeof *hud);

        hud->program = shader_data->programs[FV_SHADER_DATA_PROGRAM_HUD];
        hud->tex_width = image_width;
        hud->tex_height = image_height;

        fv_gl.glUseProgram(hud->program);
        tex_location = fv_gl.glGetUniformLocation(hud->program, "tex");
        fv_gl.glUniform1i(tex_location, 0);

        fv_gl.glGenTextures(1, &hud->tex);
        fv_gl.glBindTexture(GL_TEXTURE_2D, hud->tex);
        fv_gl.glTexImage2D(GL_TEXTURE_2D,
                           0, /* level */
                           GL_RGBA,
                           image_width, image_height,
                           0, /* border */
                           GL_RGBA,
                           GL_UNSIGNED_BYTE,
                           image);

        fv_free(image);

        fv_gl.glGenerateMipmap(GL_TEXTURE_2D);

        fv_gl.glGenVertexArrays(1, &hud->array);
        fv_gl.glBindVertexArray(hud->array);

        fv_gl.glGenBuffers(1, &hud->element_buffer);
        fv_gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hud->array);
        fv_gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                           FV_HUD_MAX_RECTANGLES * 6 * sizeof (GLubyte),
                           NULL, /* data */
                           GL_STATIC_DRAW);

        elements = fv_gl.glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);

        for (i = 0; i < FV_HUD_MAX_RECTANGLES; i++) {
                elements[i * 6 + 0] = i * 4 + 0;
                elements[i * 6 + 1] = i * 4 + 1;
                elements[i * 6 + 2] = i * 4 + 3;
                elements[i * 6 + 3] = i * 4 + 3;
                elements[i * 6 + 4] = i * 4 + 1;
                elements[i * 6 + 5] = i * 4 + 2;
        }

        fv_gl.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

        fv_gl.glGenBuffers(1, &hud->vertex_buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, hud->vertex_buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           FV_HUD_MAX_RECTANGLES * 4 *
                           sizeof (struct fv_hud_vertex),
                           NULL, /* data */
                           GL_DYNAMIC_DRAW);

        fv_gl.glEnableVertexAttribArray(0);
        fv_gl.glVertexAttribPointer(0, /* index */
                                    2, /* size */
                                    GL_FLOAT,
                                    GL_FALSE, /* normalized */
                                    sizeof (struct fv_hud_vertex),
                                    (void *) (intptr_t)
                                    offsetof(struct fv_hud_vertex, x));

        fv_gl.glEnableVertexAttribArray(1);
        fv_gl.glVertexAttribPointer(1, /* index */
                                    2, /* size */
                                    GL_FLOAT,
                                    GL_FALSE, /* normalized */
                                    sizeof (struct fv_hud_vertex),
                                    (void *) (intptr_t)
                                    offsetof(struct fv_hud_vertex, s));

        return hud;
}

static void
fv_hud_begin_rectangles(struct fv_hud *hud,
                        int screen_width,
                        int screen_height)
{
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, hud->vertex_buffer);
        hud->vertex = fv_gl.glMapBufferRange(GL_ARRAY_BUFFER,
                                             0, /* offset */
                                             sizeof (struct fv_hud_vertex) *
                                             FV_HUD_MAX_RECTANGLES * 4,
                                             GL_MAP_WRITE_BIT |
                                             GL_MAP_INVALIDATE_BUFFER_BIT |
                                             GL_MAP_FLUSH_EXPLICIT_BIT);
        hud->n_rectangles = 0;
        hud->screen_width = screen_width;
        hud->screen_height = screen_height;
}

static void
fv_hud_add_rectangle(struct fv_hud *hud,
                     int x, int y,
                     const struct fv_hud_image *image)
{
        float x1, y1, x2, y2, s1, t1, s2, t2;

        assert(hud->n_rectangles < FV_HUD_MAX_RECTANGLES);

        x1 = x * 2.0f / hud->screen_width - 1.0f;
        y1 = y * 2.0f / hud->screen_height - 1.0f;
        x2 = (x + image->w) * 2.0f / hud->screen_width - 1.0f;
        y2 = (y + image->h) * 2.0f / hud->screen_height - 1.0f;
        s1 = image->x / (float) hud->tex_width;
        t1 = (image->y + image->h) / (float) hud->tex_height;
        s2 = (image->x + image->w) / (float) hud->tex_width;
        t2 = image->y / (float) hud->tex_height;

        hud->vertex->x = x1;
        hud->vertex->y = y1;
        hud->vertex->s = s1;
        hud->vertex->t = t1;
        hud->vertex++;

        hud->vertex->x = x2;
        hud->vertex->y = y1;
        hud->vertex->s = s2;
        hud->vertex->t = t1;
        hud->vertex++;

        hud->vertex->x = x2;
        hud->vertex->y = y2;
        hud->vertex->s = s2;
        hud->vertex->t = t2;
        hud->vertex++;

        hud->vertex->x = x1;
        hud->vertex->y = y2;
        hud->vertex->s = s1;
        hud->vertex->t = t2;
        hud->vertex++;

        hud->n_rectangles++;
}

static void
fv_hud_end_rectangles(struct fv_hud *hud)
{
        fv_gl.glFlushMappedBufferRange(GL_ARRAY_BUFFER,
                                       0, /* offset */
                                       hud->n_rectangles * 4 *
                                       sizeof (struct fv_hud_vertex));
        fv_gl.glUnmapBuffer(GL_ARRAY_BUFFER);

        fv_gl.glEnable(GL_BLEND);

        fv_gl.glUseProgram(hud->program);

        fv_gl.glBindTexture(GL_TEXTURE_2D, hud->tex);

        fv_gl.glBindVertexArray(hud->array);

        fv_gl.glDrawRangeElements(GL_TRIANGLES,
                                  0, /* start */
                                  hud->n_rectangles * 4 - 1, /* end */
                                  hud->n_rectangles * 6, /* count */
                                  GL_UNSIGNED_BYTE,
                                  NULL);

        fv_gl.glDisable(GL_BLEND);
}

static void
fv_hud_add_title(struct fv_hud *hud)
{
        fv_hud_add_rectangle(hud,
                             hud->screen_width / 2 - fv_hud_image_title.w / 2,
                             hud->screen_height / 2,
                             &fv_hud_image_title);
}

void
fv_hud_paint_player_select(struct fv_hud *hud,
                           int screen_width,
                           int screen_height)
{
        fv_hud_begin_rectangles(hud, screen_width, screen_height);
        fv_hud_add_title(hud);
        fv_hud_add_rectangle(hud,
                             screen_width / 2 -
                             fv_hud_image_player_select.w / 2,
                             screen_height / 2 -
                             fv_hud_image_player_select.h -
                             10,
                             &fv_hud_image_player_select);
        fv_hud_end_rectangles(hud);
}

void
fv_hud_paint_key_select(struct fv_hud *hud,
                        int screen_width,
                        int screen_height,
                        int player_num,
                        int key_num,
                        int n_players)
{
        const struct fv_hud_image *key_image = NULL;
        int x, y;

        fv_hud_begin_rectangles(hud, screen_width, screen_height);

        fv_hud_add_title(hud);

        key_image = fv_hud_key_images[key_num];

        if (n_players == 1) {
                x = (screen_width / 2 -
                     fv_hud_image_push.w / 2 -
                     key_image->w / 2);
                y = screen_height / 2 - fv_hud_image_push.h - 10;
                fv_hud_add_rectangle(hud, x, y, &fv_hud_image_push);
                fv_hud_add_rectangle(hud,
                                     x + fv_hud_image_push.w, y,
                                     key_image);
        } else {
                x = (screen_width / 4 -
                     fv_hud_image_push.w / 2 +
                     player_num % 2 * screen_width / 2);
                y = (screen_height / 4 +
                     (1 - player_num / 2) * screen_height / 2);
                fv_hud_add_rectangle(hud, x, y, &fv_hud_image_push);
                fv_hud_add_rectangle(hud,
                                     x +
                                     (fv_hud_image_push.w - key_image->w) / 2,
                                     y - key_image->h,
                                     key_image);
        }


        fv_hud_end_rectangles(hud);
}

void
fv_hud_paint_game_state(struct fv_hud *hud,
                        int screen_width,
                        int screen_height,
                        struct fv_logic *logic)
{
}

void
fv_hud_free(struct fv_hud *hud)
{
        fv_gl.glDeleteBuffers(1, &hud->vertex_buffer);
        fv_gl.glDeleteBuffers(1, &hud->element_buffer);
        fv_gl.glDeleteVertexArrays(1, &hud->array);
        fv_gl.glDeleteTextures(1, &hud->tex);
        fv_free(hud);
}
