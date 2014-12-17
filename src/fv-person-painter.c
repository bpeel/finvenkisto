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

struct fv_person_painter {
        struct fv_model model;

        GLuint program;
        GLuint transform_uniform;
};

struct fv_person_painter *
fv_person_painter_new(struct fv_shader_data *shader_data)
{
        struct fv_person_painter *painter = fv_calloc(sizeof *painter);

        painter->program = shader_data->programs[FV_SHADER_DATA_PROGRAM_SIMPLE];
        painter->transform_uniform =
                fv_gl.glGetUniformLocation(painter->program, "transform");

        if (!fv_model_load(&painter->model, "person.ply"))
                goto error;

        return painter;

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

        fv_gl.glEnable(GL_DEPTH_TEST);

        fv_logic_for_each_person(logic, paint_person_cb, &data);

        fv_gl.glDisable(GL_DEPTH_TEST);
}

void
fv_person_painter_free(struct fv_person_painter *painter)
{
        fv_model_destroy(&painter->model);
        fv_free(painter);
}
