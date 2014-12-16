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

#include "fv-person-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-model.h"
#include "fv-gl.h"
#include "fv-image.h"

struct fv_person_painter {
        struct fv_model model;

        GLuint program;
        GLuint transform_uniform;

        GLuint texture;
};

struct fv_person_painter *
fv_person_painter_new(struct fv_shader_data *shader_data)
{
        struct fv_person_painter *painter = fv_calloc(sizeof *painter);
        GLuint tex_uniform;
        uint8_t *tex_data;
        int tex_width, tex_height;

        painter->program =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_TEXTURE];
        painter->transform_uniform =
                fv_gl.glGetUniformLocation(painter->program, "transform");

        if (!fv_model_load(&painter->model, "person.ply"))
                goto error;

        tex_data = fv_image_load("person.png", &tex_width, &tex_height, 3);
        if (tex_data == NULL)
                goto error_model;

        fv_gl.glGenTextures(1, &painter->texture);
        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);
        fv_gl.glTexImage2D(GL_TEXTURE_2D,
                           0, /* level */
                           GL_RGB,
                           tex_width, tex_height,
                           0, /* border */
                           GL_RGB,
                           GL_UNSIGNED_BYTE,
                           tex_data);
        fv_gl.glGenerateMipmap(GL_TEXTURE_2D);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MIN_FILTER,
                              GL_LINEAR_MIPMAP_NEAREST);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MAG_FILTER,
                              GL_LINEAR);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_EDGE);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_EDGE);

        fv_free(tex_data);

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
        const struct fv_transform *transform_in;
        struct fv_transform transform;
};

static void
paint_person_cb(const struct fv_logic_person *person,
                void *user_data)
{
        struct paint_closure *data = user_data;

        data->transform.modelview = data->transform_in->modelview;
        fv_matrix_translate(&data->transform.modelview,
                            person->x, person->y, 0.0f);
        fv_matrix_rotate(&data->transform.modelview,
                         person->direction * 180.f / M_PI,
                         0.0f, 0.0f, 1.0f);
        fv_transform_update_derived_values(&data->transform);

        fv_gl.glUniformMatrix4fv(data->painter->transform_uniform,
                                 1, /* count */
                                 GL_FALSE, /* transpose */
                                 &data->transform.mvp.xx);

        fv_model_paint(&data->painter->model);
}

void
fv_person_painter_paint(struct fv_person_painter *painter,
                        struct fv_logic *logic,
                        const struct fv_transform *transform)
{
        struct paint_closure data;

        data.painter = painter;
        data.transform_in = transform;
        data.transform.projection = transform->projection;

        fv_gl.glUseProgram(painter->program);

        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);

        fv_gl.glEnable(GL_DEPTH_TEST);

        fv_logic_for_each_person(logic, paint_person_cb, &data);

        fv_gl.glDisable(GL_DEPTH_TEST);
}

void
fv_person_painter_free(struct fv_person_painter *painter)
{
        fv_gl.glDeleteTextures(1, &painter->texture);
        fv_model_destroy(&painter->model);
        fv_free(painter);
}
