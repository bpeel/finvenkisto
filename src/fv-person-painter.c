/*
 * Finvenkisto
 *
 * Copyright (C) 2014 Neil Roberts
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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fv-person-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-model.h"
#include "fv-gl.h"
#include "fv-image.h"
#include "fv-error-message.h"

struct fv_person_painter {
        struct fv_model model;

        GLuint instance_buffer;

        GLuint program;

        GLuint texture;
};

struct fv_person_painter_instance {
        float mvp[16];
        uint8_t tex_layer;
        uint8_t green_tint;
};

#define FV_PERSON_PAINTER_MAX_INSTANCES 32

/* Textures to use for the different person types. These must match
 * the order of the enum in fv_person_type */
static const char *
textures[] = {
        "finvenkisto.png",
        "bambo1.png",
        "bambo2.png",
        "bambo3.png",
        "gufujestro.png",
        "toiletguy.png",
        "pyjamas.png"
};

static bool
load_texture(struct fv_person_painter *painter)
{
        int tex_width, tex_height;
        int layer_width, layer_height;
        uint8_t *tex_data;
        int i;

        fv_gl.glGenTextures(1, &painter->texture);
        fv_gl.glBindTexture(GL_TEXTURE_2D_ARRAY, painter->texture);

        for (i = 0; i < FV_N_ELEMENTS(textures); i++) {
                tex_data = fv_image_load(textures[i],
                                         &layer_width, &layer_height,
                                         3 /* components */);
                if (tex_data == NULL)
                        goto error;

                if (i == 0) {
                        tex_width = layer_width;
                        tex_height = layer_height;

                        fv_gl.glTexImage3D(GL_TEXTURE_2D_ARRAY,
                                           0, /* level */
                                           GL_RGB,
                                           tex_width, tex_height,
                                           FV_N_ELEMENTS(textures),
                                           0, /* border */
                                           GL_RGB,
                                           GL_UNSIGNED_BYTE,
                                           NULL);
                } else if (layer_width != tex_width ||
                           layer_height != tex_height) {
                        fv_error_message("Size of %s does not match that of %s",
                                         textures[i],
                                         textures[0]);
                        fv_free(tex_data);
                        goto error;
                }

                fv_gl.glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                                      0, /* level */
                                      0, 0, /* x/y offset */
                                      i, /* z offset */
                                      tex_width, tex_height,
                                      1, /* depth */
                                      GL_RGB,
                                      GL_UNSIGNED_BYTE,
                                      tex_data);

                fv_free(tex_data);
        }

        fv_gl.glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        fv_gl.glTexParameteri(GL_TEXTURE_2D_ARRAY,
                              GL_TEXTURE_MIN_FILTER,
                              GL_LINEAR_MIPMAP_NEAREST);
        fv_gl.glTexParameteri(GL_TEXTURE_2D_ARRAY,
                              GL_TEXTURE_MAG_FILTER,
                              GL_LINEAR);
        fv_gl.glTexParameteri(GL_TEXTURE_2D_ARRAY,
                              GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_EDGE);
        fv_gl.glTexParameteri(GL_TEXTURE_2D_ARRAY,
                              GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_EDGE);

        return true;

error:
        fv_gl.glDeleteTextures(1, &painter->texture);

        return false;
}

struct fv_person_painter *
fv_person_painter_new(struct fv_shader_data *shader_data)
{
        struct fv_person_painter *painter = fv_calloc(sizeof *painter);
        const size_t instance_size = sizeof (struct fv_person_painter_instance);
        const size_t matrix_offset = offsetof(struct fv_person_painter_instance,
                                              mvp[0]);
        GLuint tex_uniform;
        int i;

        painter->program =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_PERSON];

        if (!fv_model_load(&painter->model, "person.ply"))
                goto error;

        if (!load_texture(painter))
                goto error_model;

        fv_gl.glGenBuffers(1, &painter->instance_buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->instance_buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           instance_size * FV_PERSON_PAINTER_MAX_INSTANCES,
                           NULL, /* data */
                           GL_STREAM_DRAW);

        fv_gl.glBindVertexArray(painter->model.array);
        for (i = 0; i < 4; i++) {
                fv_gl.glEnableVertexAttribArray(4 + i);
                fv_gl.glVertexAttribPointer(4 + i,
                                            4, /* size */
                                            GL_FLOAT,
                                            GL_FALSE, /* normalized */
                                            instance_size,
                                            (GLvoid *) (intptr_t)
                                            (matrix_offset +
                                             sizeof (float) * i * 4));
                fv_gl.glVertexAttribDivisorARB(4 + i, 1);
        }
        fv_gl.glEnableVertexAttribArray(8);
        fv_gl.glVertexAttribPointer(8,
                                    1, /* size */
                                    GL_UNSIGNED_BYTE,
                                    GL_FALSE, /* normalized */
                                    instance_size,
                                    (GLvoid *) (intptr_t)
                                    offsetof(struct fv_person_painter_instance,
                                             tex_layer));
        fv_gl.glVertexAttribDivisorARB(8, 1);

        fv_gl.glEnableVertexAttribArray(9);
        fv_gl.glVertexAttribPointer(9,
                                    1, /* size */
                                    GL_UNSIGNED_BYTE,
                                    GL_TRUE, /* normalized */
                                    instance_size,
                                    (GLvoid *) (intptr_t)
                                    offsetof(struct fv_person_painter_instance,
                                             green_tint));
        fv_gl.glVertexAttribDivisorARB(9, 1);

        tex_uniform = fv_gl.glGetUniformLocation(painter->program, "tex");
        fv_gl.glUseProgram(painter->program);
        fv_gl.glUniform1i(tex_uniform, 0);

        return painter;

error_model:
        fv_model_destroy(&painter->model);
error:
        fv_free(painter);

        return NULL;
}

struct paint_closure {
        struct fv_person_painter *painter;
        const struct fv_paint_state *paint_state;
        struct fv_transform transform;

        struct fv_person_painter_instance *instance_buffer_map;
        int n_instances;
};

static void
flush_people(struct paint_closure *data)
{
        struct fv_person_painter *painter = data->painter;
        const size_t instance_size = sizeof (struct fv_person_painter_instance);

        if (data->n_instances == 0)
                return;

        fv_gl.glFlushMappedBufferRange(GL_ARRAY_BUFFER,
                                       0, /* offset */
                                       instance_size * data->n_instances);
        fv_gl.glUnmapBuffer(GL_ARRAY_BUFFER);

        fv_gl.glDrawElementsInstanced(GL_TRIANGLES,
                                      painter->model.n_indices,
                                      GL_UNSIGNED_SHORT,
                                      (void *) (intptr_t)
                                      painter->model.indices_offset,
                                      data->n_instances);

        data->n_instances = 0;
}

static void
paint_person_cb(const struct fv_logic_person *person,
                void *user_data)
{
        const size_t instance_size = sizeof (struct fv_person_painter_instance);
        struct fv_person_painter_instance *instance;
        struct paint_closure *data = user_data;

        /* Don't paint people that are out of the visible range */
        if (fabsf(person->x - data->paint_state->center_x) - 0.5f >=
            data->paint_state->visible_w / 2.0f ||
            fabsf(person->y - data->paint_state->center_y) - 0.5f >=
            data->paint_state->visible_h / 2.0f)
                return;

        if (data->n_instances >= FV_PERSON_PAINTER_MAX_INSTANCES)
                flush_people(data);

        if (data->n_instances == 0) {
                data->instance_buffer_map =
                        fv_gl.glMapBufferRange(GL_ARRAY_BUFFER,
                                               0, /* offset */
                                               instance_size *
                                               FV_PERSON_PAINTER_MAX_INSTANCES,
                                               GL_MAP_WRITE_BIT |
                                               GL_MAP_INVALIDATE_BUFFER_BIT |
                                               GL_MAP_FLUSH_EXPLICIT_BIT);
        }

        data->transform.modelview = data->paint_state->transform.modelview;
        fv_matrix_translate(&data->transform.modelview,
                            person->x, person->y, 0.0f);
        fv_matrix_rotate(&data->transform.modelview,
                         person->direction * 180.f / M_PI,
                         0.0f, 0.0f, 1.0f);
        fv_transform_update_derived_values(&data->transform);

        instance = data->instance_buffer_map + data->n_instances;
        memcpy(instance->mvp, &data->transform.mvp.xx, sizeof instance->mvp);
        instance->tex_layer = person->type;
        instance->green_tint = person->esperantified ? 120 : 0;

        data->n_instances++;
}

void
fv_person_painter_paint(struct fv_person_painter *painter,
                        struct fv_logic *logic,
                        const struct fv_paint_state *paint_state)
{
        struct paint_closure data;

        data.painter = painter;
        data.paint_state = paint_state;
        data.transform.projection = paint_state->transform.projection;
        data.n_instances = 0;

        fv_gl.glUseProgram(painter->program);

        fv_gl.glBindTexture(GL_TEXTURE_2D_ARRAY, painter->texture);

        fv_gl.glEnable(GL_DEPTH_TEST);

        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->instance_buffer);

        fv_gl.glBindVertexArray(painter->model.array);

        fv_logic_for_each_person(logic, paint_person_cb, &data);

        flush_people(&data);

        fv_gl.glDisable(GL_DEPTH_TEST);
}

void
fv_person_painter_free(struct fv_person_painter *painter)
{
        fv_gl.glDeleteBuffers(1, &painter->instance_buffer);
        fv_gl.glDeleteTextures(1, &painter->texture);
        fv_model_destroy(&painter->model);
        fv_free(painter);
}
