/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015, 2017 Neil Roberts
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
#include "fv-vk-data.h"
#include "fv-pipeline-data.h"
#include "fv-error-message.h"
#include "fv-vertex.h"
#include "fv-allocate-store.h"

struct property {
        int n_components;
        const char *components[3];
        enum { PROPERTY_FLOAT, PROPERTY_BYTE } type;
        size_t offset;
};

static const struct property
properties[] = {
        { 3, { "x", "y", "z" }, PROPERTY_FLOAT,
          offsetof(struct fv_vertex_model_color, x) },
        { 2, { "s", "t" }, PROPERTY_FLOAT,
          offsetof(struct fv_vertex_model_texture, s) },
        { 3, { "nx", "ny", "nz" }, PROPERTY_FLOAT,
          offsetof(struct fv_vertex_model_color, nx) },
        { 3, { "red", "green", "blue" }, PROPERTY_BYTE,
          offsetof(struct fv_vertex_model_color, r) },
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
        enum fv_model_type type;

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
        uint8_t *prop_offset;

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

        prop_offset = data->current_vertex + properties[prop_num].offset;
        value = ply_get_argument_value(argument);

        switch (properties[prop_num].type) {
        case PROPERTY_FLOAT:
                ((float *) prop_offset)[comp_num] = value;
                break;
        case PROPERTY_BYTE:
                ((uint8_t *) prop_offset)[comp_num] = value;
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
        bool have_texture_coords = false;

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

                        if (prop == 1)
                                have_texture_coords = true;
                }
        }

        n_instances = ply_set_read_cb(data->ply,
                                      "face", "vertex_indices",
                                      face_read_cb,
                                      data, 0);

        if (have_texture_coords) {
                data->type = FV_MODEL_TYPE_TEXTURE;
                data->vertex_size = sizeof (struct fv_vertex_model_texture);
        } else {
                data->type = FV_MODEL_TYPE_COLOR;
                data->vertex_size = sizeof (struct fv_vertex_model_color);
        }

        return true;
}

static bool
create_buffer(const struct fv_vk_data *vk_data,
              struct data *data)
{
        struct fv_model *model = data->model;
        void *memory_map;
        VkResult res;
        int buffer_offset;

        model->n_vertices = data->n_vertices;
        model->n_indices = data->indices.length / sizeof (uint16_t);
        model->type = data->type;
        model->vertices_offset = 0;
        model->indices_offset = data->n_vertices * data->vertex_size;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = model->indices_offset + data->indices.length,
                .usage = (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        res = fv_vk.vkCreateBuffer(vk_data->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &model->buffer);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating model buffer");
                goto error;
        }

        res = fv_allocate_store_buffer(vk_data,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &model->buffer,
                                       &model->memory,
                                       &buffer_offset);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating model memory");
                goto error_buffer;
        }

        res = fv_vk.vkMapMemory(vk_data->device,
                                model->memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                &memory_map);
        if (res != VK_SUCCESS) {
                fv_error_message("Error mapping map memory");
                goto error_memory;
        }

        memcpy((uint8_t *) memory_map + model->vertices_offset,
               data->vertices,
               data->n_vertices * data->vertex_size);
        memcpy((uint8_t *) memory_map + model->indices_offset,
               data->indices.data,
               data->indices.length);

        fv_vk.vkUnmapMemory(vk_data->device, model->memory);

        return true;

error_memory:
        fv_vk.vkFreeMemory(vk_data->device,
                           model->memory,
                           NULL /* allocator */);
error_buffer:
        fv_vk.vkDestroyBuffer(vk_data->device,
                              model->buffer,
                              NULL /* allocator */);
error:
        return false;
}

bool
fv_model_load(const struct fv_vk_data *vk_data,
              struct fv_model *model,
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
                        create_buffer(vk_data, &data);

                fv_buffer_destroy(&data.indices);

                fv_free(data.vertices);
        }

        ply_close(data.ply);

        return !data.had_error;
}

void
fv_model_destroy(const struct fv_vk_data *vk_data,
                 struct fv_model *model)
{
        fv_vk.vkDestroyBuffer(vk_data->device,
                              model->buffer,
                              NULL /* allocator */);
        fv_vk.vkFreeMemory(vk_data->device,
                           model->memory,
                           NULL /* allocator */);
}
