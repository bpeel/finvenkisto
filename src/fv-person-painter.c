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

#include <epoxy/gl.h>

#include "fv-person-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-model.h"

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
                glGetUniformLocation(painter->program, "transform");

        if (!fv_model_load(&painter->model, "person.ply"))
                goto error;

        return painter;

error:
        fv_free(painter);

        return NULL;
}

void
fv_person_painter_paint(struct fv_person_painter *painter,
                        struct fv_logic *logic,
                        const struct fv_transform *transform_in)
{
        struct fv_transform transform;
        float center_x, center_y;

        fv_logic_get_center(logic, &center_x, &center_y);

        transform.projection = transform_in->projection;
        transform.modelview = transform_in->modelview;
        fv_matrix_translate(&transform.modelview,
                            center_x, center_y, 0.0f);
        fv_transform_update_derived_values(&transform);

        glUseProgram(painter->program);
        glUniformMatrix4fv(painter->transform_uniform,
                           1, /* count */
                           GL_FALSE, /* transpose */
                           &transform.mvp.xx);

        glEnable(GL_DEPTH_TEST);

        fv_model_paint(&painter->model);

        glDisable(GL_DEPTH_TEST);
}

void
fv_person_painter_free(struct fv_person_painter *painter)
{
        fv_model_destroy(&painter->model);
        fv_free(painter);
}
