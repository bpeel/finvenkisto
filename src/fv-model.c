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

#include <rply/rply.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "fv-model.h"
#include "fv-util.h"
#include "fv-data.h"
#include "fv-buffer.h"
#include "fv-gl.h"
#include "fv-shader-data.h"
#include "fv-error-message.h"

struct property {
        int n_components;
        const char *components[3];
        enum { PROPERTY_FLOAT, PROPERTY_BYTE } type;
        GLint attrib_location;
};

static const struct property
properties[] = {
        /* These should be sorted in descending order of size so that
           it never ends doing an unaligned write */
        { 3, { "x", "y", "z" }, PROPERTY_FLOAT,
          FV_SHADER_DATA_ATTRIB_POSITION },
        { 2, { "s", "t" }, PROPERTY_FLOAT,
          FV_SHADER_DATA_ATTRIB_TEX_COORD },
        { 3, { "nx", "ny", "nz" }, PROPERTY_FLOAT,
          FV_SHADER_DATA_ATTRIB_NORMAL },
        { 3, { "red", "green", "blue" }, PROPERTY_BYTE,
          FV_SHADER_DATA_ATTRIB_COLOR }
};

#define N_PROPERTIES (FV_N_ELEMENTS(properties))

struct data {
        struct fv_model *model;

        const char *filename;

        p_ply ply;

        bool had_error;

        int n_available_components;
        int n_got_components;
        int vertex_size;
        int available_props;

        int property_offsets[N_PROPERTIES];

        long n_vertices;
        uint8_t *current_vertex;
        uint8_t *vertices;
        struct fv_buffer indices;

        int first_vertex;
        int last_vertex;
};

static void
error_cb(const char *message,
         void *user_data)
{
        struct data *data = user_data;

        fv_error_message("%s: %s", data->filename, message);

        data->had_error = true;
}

static int
vertex_read_cb(p_ply_argument argument)
{
        long prop_comp_num;
        int prop_num, comp_num;
        struct data *data;
        int32_t length, index;
        double value;

        ply_get_argument_user_data(argument, (void **) &data, &prop_comp_num);
        ply_get_argument_property(argument, NULL, &length, &index);

        prop_num = prop_comp_num >> 8;
        comp_num = prop_comp_num & 0xff;

        assert(data->current_vertex <
               data->vertices + data->n_vertices * data->vertex_size);

        if (length != 1 || index != 0) {
                fv_error_message("%s: List type property not expected for "
                                 "vertex element '%s'",
                                 data->filename,
                                 properties[prop_num].components[comp_num]);
                data->had_error = true;

                return 0;
        }

        value = ply_get_argument_value(argument);

        switch (properties[prop_num].type) {
        case PROPERTY_FLOAT:
                ((float *) (data->current_vertex +
                            data->property_offsets[prop_num]))[comp_num] =
                        value;
                break;
        case PROPERTY_BYTE:
                ((uint8_t *) (data->current_vertex +
                              data->property_offsets[prop_num]))[comp_num] =
                        value;
                break;
        }

        data->n_got_components++;

        /* If we've got enough properties for a complete vertex then
         * move on to the next one */
        if (data->n_got_components == data->n_available_components) {
                data->current_vertex += data->vertex_size;
                data->n_got_components = 0;
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
                fv_error_message("%s: index value out of range",
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
        int prop, comp;
        const char *component;
        long n_instances;

        for (prop = 0; prop < N_PROPERTIES; prop++) {
                for (comp = 0; comp < properties[prop].n_components; comp++) {
                        component = properties[prop].components[comp];
                        n_instances = ply_set_read_cb(data->ply,
                                                      "vertex",
                                                      component,
                                                      vertex_read_cb,
                                                      data,
                                                      (prop << 8) | comp);
                        if (n_instances == 0) {
                                if (comp > 0) {
                                        fv_error_message("%s: Missing "
                                                         "component ‘%s’",
                                                         data->filename,
                                                         component);
                                        return false;
                                }
                                break;
                        } else if (n_instances > UINT16_MAX) {
                                fv_error_message("%s: Too many vertices to fit "
                                                 "in a uint16_t",
                                                 data->filename);
                                return false;
                        }

                        data->n_vertices = n_instances;

                        data->n_available_components++;

                        if (comp == 0) {
                                data->property_offsets[prop] =
                                        data->vertex_size;
                                data->available_props |= (1 << prop);
                        }

                        switch (properties[prop].type) {
                        case PROPERTY_FLOAT:
                                data->vertex_size += sizeof (float);
                                break;
                        case PROPERTY_BYTE:
                                data->vertex_size += sizeof (uint8_t);
                                break;
                        }
                }
        }

        n_instances = ply_set_read_cb(data->ply,
                                      "face", "vertex_indices",
                                      face_read_cb,
                                      data, 0);

        /* Align the vertex size to the size of a float */
        data->vertex_size = ((data->vertex_size + sizeof (float) - 1) &
                             ~(sizeof (float) - 1));

        return true;
}

static void
create_buffer(struct data *data)
{
        struct fv_model *model = data->model;
        GLenum type = GL_FLOAT;
        GLboolean normalized = GL_FALSE;
        GLint attrib;
        int i;

        model->indices_offset = data->n_vertices * data->vertex_size;
        model->n_vertices = data->n_vertices;
        model->n_indices = data->indices.length / sizeof (uint16_t);

        model->array = fv_array_object_new();

        fv_gl.glGenBuffers(1, &model->buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, model->buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           model->indices_offset +
                           data->indices.length,
                           NULL, /* data */
                           GL_STATIC_DRAW);
        fv_gl.glBufferSubData(GL_ARRAY_BUFFER,
                              0, /* offset */
                              model->indices_offset, /* length */
                              data->vertices);
        fv_gl.glBufferSubData(GL_ARRAY_BUFFER,
                              model->indices_offset,
                              data->indices.length,
                              data->indices.data);

        for (i = 0; i < N_PROPERTIES; i++) {
                if (!(data->available_props & (1 << i)))
                        continue;

                switch (properties[i].type) {
                case PROPERTY_FLOAT:
                        type = GL_FLOAT;
                        normalized = GL_FALSE;
                        break;
                case PROPERTY_BYTE:
                        type = GL_UNSIGNED_BYTE;
                        normalized = GL_TRUE;
                        break;
                }

                attrib = properties[i].attrib_location;

                fv_array_object_set_attribute(model->array,
                                              attrib, /* index */
                                              properties[i].n_components,
                                              type,
                                              normalized,
                                              data->vertex_size,
                                              0, /* divisor */
                                              model->buffer,
                                              data->property_offsets[i]);
        }

        fv_array_object_set_element_buffer(model->array, model->buffer);
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

        data.n_available_components = 0;
        data.n_got_components = 0;
        data.vertex_size = 0;
        data.available_props = 0;

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
                data.vertices = fv_alloc(data.vertex_size * data.n_vertices);
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
        fv_array_object_bind(model->array);

        fv_gl_draw_range_elements(GL_TRIANGLES,
                                  0, model->n_vertices - 1,
                                  model->n_indices,
                                  GL_UNSIGNED_SHORT,
                                  (void *) (intptr_t)
                                  model->indices_offset);
}

void
fv_model_destroy(struct fv_model *model)
{
        fv_array_object_free(model->array);
        fv_gl.glDeleteBuffers(1, &model->buffer);
}
