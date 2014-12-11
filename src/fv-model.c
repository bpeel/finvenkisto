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
#include <rply/rply.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "fv-model.h"
#include "fv-util.h"
#include "fv-data.h"
#include "fv-buffer.h"

struct vertex {
        float x, y, z;
        float nx, ny, nz;
};

struct property {
        const char *name;
        int offset;
        enum { PROPERTY_FLOAT, PROPERTY_BYTE } type;
};

struct data {
        struct fv_model *model;

        const char *filename;

        p_ply ply;

        bool had_error;

        int got_props;
        long n_vertices;
        struct vertex *current_vertex;
        struct vertex *vertices;
        struct fv_buffer indices;

        int first_vertex;
        int last_vertex;
};

static const struct property
properties[] = {
        { "x", offsetof(struct vertex, x), PROPERTY_FLOAT },
        { "y", offsetof(struct vertex, y), PROPERTY_FLOAT },
        { "z", offsetof(struct vertex, z), PROPERTY_FLOAT },
        { "nx", offsetof(struct vertex, nx), PROPERTY_FLOAT },
        { "ny", offsetof(struct vertex, ny), PROPERTY_FLOAT },
        { "nz", offsetof(struct vertex, nz), PROPERTY_FLOAT },
};

static void
error_cb(const char *message,
         void *user_data)
{
        struct data *data = user_data;

        fprintf(stderr, "%s: %s\n", data->filename, message);

        data->had_error = true;
}

static int
vertex_read_cb(p_ply_argument argument)
{
        long prop_num;
        struct data *data;
        int32_t length, index;
        double value;

        ply_get_argument_user_data(argument, (void **) &data, &prop_num);
        ply_get_argument_property(argument, NULL, &length, &index);

        assert(data->current_vertex < data->vertices + data->n_vertices);

        if (length != 1 || index != 0) {
                fprintf(stderr,
                        "%s: List type property not expected for "
                        "vertex element '%s'",
                        data->filename,
                        properties[prop_num].name);
                data->had_error = true;

                return 0;
        }

        value = ply_get_argument_value(argument);

        switch (properties[prop_num].type) {
        case PROPERTY_FLOAT:
                *(float *) ((uint8_t *) data->current_vertex +
                            properties[prop_num].offset) = value;
                break;
        case PROPERTY_BYTE:
                *((uint8_t *) data->current_vertex +
                  properties[prop_num].offset) = value;
                break;
        }

        data->got_props |= 1 << prop_num;

        /* If we've got enough properties for a complete vertex then
         * move on to the next one */
        if (data->got_props == (1 << FV_N_ELEMENTS(properties)) - 1) {
                data->current_vertex++;
                data->got_props = 0;
        }

        return 1;
}

static int
face_read_cb(p_ply_argument argument)
{
        long prop_num;
        struct data *data;
        int32_t length, index;
        long value;
        uint16_t *indices;

        ply_get_argument_user_data(argument, (void **) &data, &prop_num);
        ply_get_argument_property(argument, NULL, &length, &index);

        value = ply_get_argument_value(argument);
        if (value < 0 || value >= data->n_vertices) {
                fprintf(stderr,
                        "%s: index value out of range\n",
                        data->filename);
                return 0;
        }

        if (index == 0) {
                data->first_vertex = value;
        } else if (index == 1) {
                data->last_vertex = value;
        } else if (index != -1) {
                /* Add a triangle with the first vertex, the last
                 * vertex and this new vertex as if it was a triangle
                 * fan */

                fv_buffer_set_length(&data->indices,
                                     data->indices.length +
                                     sizeof (uint16_t) * 3);
                indices = (uint16_t *) (data->indices.data +
                                        data->indices.length -
                                        sizeof (uint16_t) * 3);
                *(indices++) = data->first_vertex;
                *(indices++) = data->last_vertex;
                *(indices++) = value;

                /* Use the new vertex as one of the vertices next time
                 * around */
                data->last_vertex = value;
        }

        return 1;
}

static bool
set_property_callbacks(struct data *data)
{
        int i;
        long n_instances;

        for (i = 0; i < FV_N_ELEMENTS(properties); i++) {
                n_instances = ply_set_read_cb(data->ply,
                                              "vertex",
                                              properties[i].name,
                                              vertex_read_cb,
                                              data, i);
                if (n_instances == 0) {
                        fprintf(stderr,
                                "%s: Missing property ‘%s’\n",
                                data->filename,
                                properties[i].name);
                        return false;
                } else if (n_instances > UINT16_MAX) {
                        fprintf(stderr,
                                "%s: Too many vertices to fit in a uint16_t\n",
                                data->filename);
                        return false;
                }

                data->n_vertices = n_instances;
        }

        n_instances = ply_set_read_cb(data->ply,
                                      "face", "vertex_indices",
                                      face_read_cb,
                                      data, 0);

        return true;
}

static void
create_buffer(struct data *data)
{
        struct fv_model *model = data->model;

        model->indices_offset = data->n_vertices * sizeof (struct vertex);
        model->n_vertices = data->n_vertices;
        model->n_indices = data->indices.length / sizeof (uint16_t);

        glGenVertexArrays(1, &model->array);
        glBindVertexArray(model->array);

        glGenBuffers(1, &model->buffer);
        glBindBuffer(GL_ARRAY_BUFFER, model->buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     model->indices_offset +
                     data->indices.length,
                     NULL, /* data */
                     GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER,
                        0, /* offset */
                        model->indices_offset, /* length */
                        data->vertices);
        glBufferSubData(GL_ARRAY_BUFFER,
                        model->indices_offset,
                        data->indices.length,
                        data->indices.data);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, /* index */
                              3, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* normalized */
                              sizeof (struct vertex),
                              (void *) (intptr_t)
                              offsetof(struct vertex, x));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, /* index */
                              3, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* normalized */
                              sizeof (struct vertex),
                              (void *) (intptr_t)
                              offsetof(struct vertex, nx));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->buffer);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool
fv_model_load(struct fv_model *model,
              const char *filename)
{
        char *full_filename = fv_data_get_filename(filename);
        struct data data;

        data.had_error = false;
        data.model = model;
        data.filename = filename;
        data.got_props = 0;

        if (full_filename == NULL)
                return false;

        data.ply = ply_open(full_filename, error_cb, &data);

        fv_free(full_filename);

        if (!data.ply)
                return false;

        if (!ply_read_header(data.ply) ||
            !set_property_callbacks(&data)) {
                data.had_error = true;
        } else {
                data.vertices = fv_alloc(sizeof (struct vertex) *
                                         data.n_vertices);
                data.current_vertex = data.vertices;

                fv_buffer_init(&data.indices);

                if (!ply_read(data.ply))
                        data.had_error = true;
                else
                        create_buffer(&data);

                fv_buffer_destroy(&data.indices);

                fv_free(data.vertices);
        }

        ply_close(data.ply);

        return !data.had_error;
}

void
fv_model_paint(const struct fv_model *model)
{
        glBindVertexArray(model->array);

        glDrawRangeElements(GL_TRIANGLES,
                            0, model->n_vertices - 1,
                            model->n_indices,
                            GL_UNSIGNED_SHORT,
                            (void *) (intptr_t)
                            model->indices_offset);
}

void
fv_model_destroy(struct fv_model *model)
{
        glDeleteVertexArrays(1, &model->array);
        glDeleteBuffers(1, &model->buffer);
}
