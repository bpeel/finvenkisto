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
#include <stddef.h>

#include "fv-person-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-model.h"
#include "fv-gl.h"
#include "fv-error-message.h"
#include "fv-map-buffer.h"

/* Textures to use for the different person types. These must match
 * the order of the enum in fv_person_type */
enum fv_image_data_image
textures[] = {
        FV_IMAGE_DATA_FINVENKISTO,
        FV_IMAGE_DATA_BAMBO1,
        FV_IMAGE_DATA_BAMBO2,
        FV_IMAGE_DATA_BAMBO3,
        FV_IMAGE_DATA_GUFUJESTRO,
        FV_IMAGE_DATA_TOILETGUY,
        FV_IMAGE_DATA_PYJAMAS,
};

struct fv_person_painter {
        struct fv_model model;

        GLuint instance_buffer;

        GLuint program;

        GLuint textures[FV_N_ELEMENTS(textures)];

        GLuint transform_uniform;
        GLuint green_tint_uniform;
        GLuint normal_transform_uniform;

        bool use_instancing;
};

struct fv_person_painter_instance {
        float mvp[16];
        float normal_transform[3 * 3];
        uint8_t tex_layer;
        uint8_t green_tint;
};

#define FV_PERSON_PAINTER_MAX_INSTANCES 32

static void
set_texture_properties(GLenum target)
{
        fv_gl.glTexParameteri(target,
                              GL_TEXTURE_MIN_FILTER,
                              GL_LINEAR_MIPMAP_NEAREST);
        fv_gl.glTexParameteri(target,
                              GL_TEXTURE_MAG_FILTER,
                              GL_LINEAR);
        fv_gl.glTexParameteri(target,
                              GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_EDGE);
        fv_gl.glTexParameteri(target,
                              GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_EDGE);
}

static void
set_texture(struct fv_person_painter *painter,
            struct fv_image_data *image_data,
            int tex_num,
            int tex_width, int tex_height)
{
        if (painter->use_instancing) {
                if (tex_num == 0) {
                        fv_gl.glTexImage3D(GL_TEXTURE_2D_ARRAY,
                                           0, /* level */
                                           GL_RGB,
                                           tex_width, tex_height,
                                           FV_N_ELEMENTS(textures),
                                           0, /* border */
                                           GL_RGB,
                                           GL_UNSIGNED_BYTE,
                                           NULL);
                }
                fv_image_data_set_sub_3d(image_data,
                                         GL_TEXTURE_2D_ARRAY,
                                         0, /* level */
                                         0, 0, /* x/y offset */
                                         tex_num, /* z offset */
                                         textures[tex_num]);
        } else {
                fv_gl.glBindTexture(GL_TEXTURE_2D, painter->textures[tex_num]);
                fv_image_data_set_2d(image_data,
                                     GL_TEXTURE_2D,
                                     0, /* level */
                                     GL_RGB,
                                     textures[tex_num]);
                set_texture_properties(GL_TEXTURE_2D);
                fv_gl.glGenerateMipmap(GL_TEXTURE_2D);
        }
}

static bool
load_textures(struct fv_person_painter *painter,
              struct fv_image_data *image_data)
{
        int tex_width = -1, tex_height = -1;
        int layer_width, layer_height;
        int i;

        if (painter->use_instancing) {
                fv_gl.glGenTextures(1, painter->textures);
                fv_gl.glBindTexture(GL_TEXTURE_2D_ARRAY, painter->textures[0]);
        } else {
                fv_gl.glGenTextures(FV_N_ELEMENTS(textures), painter->textures);
        }

        for (i = 0; i < FV_N_ELEMENTS(textures); i++) {
                fv_image_data_get_size(image_data,
                                       textures[i],
                                       &layer_width, &layer_height);

                if (i > 0 &&
                    (layer_width != tex_width ||
                     layer_height != tex_height)) {
                        fv_error_message("Person textures are not all "
                                         "the same size");
                        goto error;
                } else {
                        tex_width = layer_width;
                        tex_height = layer_height;
                }

                set_texture(painter,
                            image_data,
                            i,
                            tex_width, tex_height);
        }

        if (painter->use_instancing) {
                set_texture_properties(GL_TEXTURE_2D_ARRAY);
                fv_gl.glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        }

        return true;

error:
        fv_gl.glDeleteTextures(painter->use_instancing
                               ? 1 : FV_N_ELEMENTS(textures),
                               painter->textures);

        return false;
}

static void
set_up_instanced_arrays(struct fv_person_painter *painter)
{
        GLint attrib;
        const size_t instance_size = sizeof (struct fv_person_painter_instance);
        const size_t matrix_offset =
                offsetof(struct fv_person_painter_instance, mvp[0]);
        const size_t normal_transform_offset =
                offsetof(struct fv_person_painter_instance,
                         normal_transform[0]);
        const size_t tex_layer_offset =
                offsetof(struct fv_person_painter_instance, tex_layer);
        const size_t green_tint_offset =
                offsetof(struct fv_person_painter_instance, green_tint);
        int i;

        attrib = fv_gl.glGetAttribLocation(painter->program, "transform");

        for (i = 0; i < 4; i++) {
                fv_array_object_set_attribute(painter->model.array,
                                              attrib + i,
                                              4, /* size */
                                              GL_FLOAT,
                                              GL_FALSE, /* normalized */
                                              instance_size,
                                              1, /* divisor */
                                              painter->instance_buffer,
                                              (matrix_offset +
                                               sizeof (float) * i * 4));
        }

        attrib = fv_gl.glGetAttribLocation(painter->program,
                                           "normal_transform");

        for (i = 0; i < 3; i++) {
                fv_array_object_set_attribute(painter->model.array,
                                              attrib + i,
                                              3, /* size */
                                              GL_FLOAT,
                                              GL_FALSE, /* normalized */
                                              instance_size,
                                              1, /* divisor */
                                              painter->instance_buffer,
                                              (normal_transform_offset +
                                               sizeof (float) * i * 3));
        }

        attrib = fv_gl.glGetAttribLocation(painter->program, "tex_layer");

        fv_array_object_set_attribute(painter->model.array,
                                      attrib,
                                      1, /* size */
                                      GL_UNSIGNED_BYTE,
                                      GL_FALSE, /* normalized */
                                      instance_size,
                                      1, /* divisor */
                                      painter->instance_buffer,
                                      tex_layer_offset);

        attrib = fv_gl.glGetAttribLocation(painter->program,
                                           "green_tint_attrib");

        fv_array_object_set_attribute(painter->model.array,
                                      attrib,
                                      1, /* size */
                                      GL_UNSIGNED_BYTE,
                                      GL_TRUE, /* normalized */
                                      instance_size,
                                      1, /* divisor */
                                      painter->instance_buffer,
                                      green_tint_offset);
}

struct fv_person_painter *
fv_person_painter_new(struct fv_image_data *image_data,
                      struct fv_shader_data *shader_data)
{
        struct fv_person_painter *painter = fv_calloc(sizeof *painter);
        GLuint tex_uniform;

        painter->use_instancing =
                fv_gl.have_instanced_arrays &&
                fv_gl.have_texture_2d_array;

        painter->program =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_PERSON];

        if (!fv_model_load(&painter->model, "person.ply"))
                goto error;

        if (!load_textures(painter, image_data))
                goto error_model;

        if (painter->use_instancing) {
                fv_gl.glGenBuffers(1, &painter->instance_buffer);
                fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->instance_buffer);
                fv_gl.glBufferData(GL_ARRAY_BUFFER,
                                   sizeof (struct fv_person_painter_instance) *
                                   FV_PERSON_PAINTER_MAX_INSTANCES,
                                   NULL, /* data */
                                   GL_STREAM_DRAW);

                set_up_instanced_arrays(painter);
        } else {
                painter->transform_uniform =
                        fv_gl.glGetUniformLocation(painter->program,
                                                   "transform");
                painter->green_tint_uniform =
                        fv_gl.glGetUniformLocation(painter->program,
                                                   "green_tint_attrib");
                painter->normal_transform_uniform =
                        fv_gl.glGetUniformLocation(painter->program,
                                                   "normal_transform");
        }

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

        fv_map_buffer_flush(0, /* offset */
                            instance_size * data->n_instances);
        fv_map_buffer_unmap();

        fv_gl.glDrawElementsInstanced(GL_TRIANGLES,
                                      painter->model.n_indices,
                                      GL_UNSIGNED_SHORT,
                                      NULL, /* offset */
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
        GLsizei buffer_size;
        float green_tint;
        GLuint uniform;

        /* Don't paint people that are out of the visible range */
        if (fabsf(person->x - data->paint_state->center_x) - 0.5f >=
            data->paint_state->visible_w / 2.0f ||
            fabsf(person->y - data->paint_state->center_y) - 0.5f >=
            data->paint_state->visible_h / 2.0f)
                return;

        if (data->n_instances >= FV_PERSON_PAINTER_MAX_INSTANCES)
                flush_people(data);

        data->transform.modelview = data->paint_state->transform.modelview;
        fv_matrix_translate(&data->transform.modelview,
                            person->x, person->y, 0.0f);
        fv_matrix_rotate(&data->transform.modelview,
                         person->direction * 180.f / M_PI,
                         0.0f, 0.0f, 1.0f);
        fv_transform_dirty(&data->transform);
        fv_transform_ensure_mvp(&data->transform);
        fv_transform_ensure_normal_transform(&data->transform);

        green_tint = person->esperantified ? 120 : 0;

        if (data->painter->use_instancing) {
                if (data->n_instances == 0) {
                        buffer_size = (instance_size *
                                       FV_PERSON_PAINTER_MAX_INSTANCES);
                        data->instance_buffer_map =
                                fv_map_buffer_map(GL_ARRAY_BUFFER,
                                                  buffer_size,
                                                  true /* flush_explicit */,
                                                  GL_STREAM_DRAW);
                }

                instance = data->instance_buffer_map + data->n_instances;
                memcpy(instance->mvp,
                       &data->transform.mvp.xx,
                       sizeof instance->mvp);
                memcpy(instance->normal_transform,
                       data->transform.normal_transform,
                       sizeof instance->normal_transform);
                instance->tex_layer = person->type;
                instance->green_tint = green_tint;

                data->n_instances++;
        } else {
                fv_gl.glBindTexture(GL_TEXTURE_2D,
                                    data->painter->textures[person->type]);
                uniform = data->painter->transform_uniform;
                fv_gl.glUniformMatrix4fv(uniform,
                                         1, /* count */
                                         GL_FALSE, /* transpose */
                                         &data->transform.mvp.xx);
                fv_gl.glUniform1f(data->painter->green_tint_uniform,
                                  green_tint / 255.0f);
                uniform = data->painter->normal_transform_uniform;
                fv_gl.glUniformMatrix3fv(uniform,
                                         1, /* count */
                                         GL_FALSE, /* transpose */
                                         data->transform.normal_transform);
                fv_model_paint(&data->painter->model);
        }
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

        fv_gl.glEnable(GL_DEPTH_TEST);

        if (painter->use_instancing) {
                fv_gl.glBindTexture(GL_TEXTURE_2D_ARRAY, painter->textures[0]);
                fv_array_object_bind(painter->model.array);
                fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->instance_buffer);
        }

        fv_logic_for_each_person(logic, paint_person_cb, &data);

        flush_people(&data);

        fv_gl.glDisable(GL_DEPTH_TEST);
}

void
fv_person_painter_free(struct fv_person_painter *painter)
{
        if (painter->use_instancing)
                fv_gl.glDeleteBuffers(1, &painter->instance_buffer);
        fv_gl.glDeleteTextures(painter->use_instancing
                               ? 1 : FV_N_ELEMENTS(textures),
                               painter->textures);
        fv_model_destroy(&painter->model);
        fv_free(painter);
}
