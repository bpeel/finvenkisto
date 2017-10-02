/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015, 2016, 2017 Neil Roberts
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
#include <limits.h>
#include <assert.h>
#include <string.h>

#include "fv-map-painter.h"
#include "fv-map.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-vk.h"
#include "fv-vertex.h"
#include "fv-error-message.h"
#include "fv-allocate-store.h"
#include "fv-list.h"
#include "fv-model.h"
#include "fv-flush-memory.h"

#define FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE 64
#define FV_MAP_PAINTER_INSTANCES_PER_BUFFER 8

#define FV_MAP_PAINTER_N_MODELS FV_N_ELEMENTS(fv_map_painter_models)

/* The normals for the map are only ever one of the the following
 * directions so instead of encoding each component of the normal in
 * the vertex we just encode a byte with one of the following values
 * and let the vertex shader expand it out.
 */
#define FV_MAP_PAINTER_NORMAL_UP 0
#define FV_MAP_PAINTER_NORMAL_NORTH 166
#define FV_MAP_PAINTER_NORMAL_EAST 255
#define FV_MAP_PAINTER_NORMAL_SOUTH 90
#define FV_MAP_PAINTER_NORMAL_WEST 3

struct fv_map_painter_model {
        const char *filename;
        enum fv_image_data_image texture;
};

static struct fv_map_painter_model
fv_map_painter_models[] = {
        { "table.ply", 0 },
        { "toilet.ply", 0 },
        { "teaset.ply", 0 },
        { "chair.ply", 0 },
        { "bed.ply", 0 },
        { "barrel.ply", 0 },
};

struct fv_map_painter_tile {
        int offset;
        int count;
};

struct fv_map_painter_special {
        struct fv_model model;
};

struct instance_buffer {
        struct fv_list link;
        VkBuffer buffer;
        VkDeviceMemory memory;
        VkDeviceSize watermark;
        int memory_type_index;
};

struct fv_map_painter {
        struct fv_map_painter_tile tiles[FV_MAP_TILES_X *
                                         FV_MAP_TILES_Y];

        const struct fv_vk_data *vk_data;

        VkPipeline map_pipeline;
        VkPipelineLayout map_layout;
        VkBuffer map_buffer;
        VkDeviceMemory map_memory;
        VkImage texture_image;
        VkDeviceMemory texture_memory;
        VkImageView texture_view;
        VkDescriptorSet descriptor_set;
        VkPipeline color_pipeline;

        struct fv_list instance_buffers;
        struct fv_list in_use_instance_buffers;
        struct fv_instance_special *instance_buffer_map;
        int n_instances;
        int current_special;
        int instance_buffer_offset;

        struct fv_map_painter_special specials[FV_MAP_PAINTER_N_MODELS];

        VkDeviceSize vertices_offset;

        int texture_width, texture_height;
};

struct tile_data {
        struct fv_buffer indices;
        struct fv_buffer vertices;
};

static float
get_block_height(fv_map_block_t block)
{
        switch (FV_MAP_GET_BLOCK_TYPE(block)) {
        case FV_MAP_BLOCK_TYPE_FULL_WALL:
                return 2.0f;
        case FV_MAP_BLOCK_TYPE_HALF_WALL:
                return 1.0f;
        default:
                return 0.0f;
        }
}

static float
get_position_height(int x, int y)
{
        if (x < 0 || x >= FV_MAP_WIDTH ||
            y < 0 || y >= FV_MAP_HEIGHT)
                return 0.0f;

        return get_block_height(fv_map[y * FV_MAP_WIDTH + x]);
}

static struct fv_vertex_map *
reserve_quad(struct tile_data *data)
{
        struct fv_vertex_map *v;
        uint16_t *idx;
        size_t v1, i1;

        v1 = data->vertices.length / sizeof (struct fv_vertex_map);
        fv_buffer_set_length(&data->vertices,
                             sizeof (struct fv_vertex_map) * (v1 + 4));
        v = (struct fv_vertex_map *) data->vertices.data + v1;

        i1 = data->indices.length / sizeof (uint16_t);
        fv_buffer_set_length(&data->indices,
                             sizeof (uint16_t) * (i1 + 6));
        idx = (uint16_t *) data->indices.data + i1;

        *(idx++) = v1 + 0;
        *(idx++) = v1 + 1;
        *(idx++) = v1 + 2;
        *(idx++) = v1 + 2;
        *(idx++) = v1 + 1;
        *(idx++) = v1 + 3;

        return v;
}

static struct fv_vertex_map *
add_horizontal_side(struct tile_data *data,
                    int y,
                    int x1, int z1,
                    int x2, int z2)
{
        struct fv_vertex_map *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++)
                v[i].y = y;

        v[0].x = x1;
        v[0].z = z1;
        v[1].x = x2;
        v[1].z = z1;
        v[2].x = x1;
        v[2].z = z2;
        v[3].x = x2;
        v[3].z = z2;

        return v;
}

static struct fv_vertex_map *
add_vertical_side(struct tile_data *data,
                  int x,
                  int y1, int z1,
                  int y2, int z2)
{
        struct fv_vertex_map *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++)
                v[i].x = x;

        v[0].y = y1;
        v[0].z = z1;
        v[1].y = y2;
        v[1].z = z1;
        v[2].y = y1;
        v[2].z = z2;
        v[3].y = y2;
        v[3].z = z2;

        return v;
}

static void
set_tex_coords_for_image(struct fv_map_painter *painter,
                         struct fv_vertex_map v[4],
                         int image,
                         int height)
{
        int blocks_h = (painter->texture_height /
                        FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE);
        int is1 = image / blocks_h * FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE * 2;
        int it1 = image % blocks_h * FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE;
        uint16_t s1 = is1 * (UINT16_MAX - 1) / painter->texture_width;
        uint16_t t1 = it1 * (UINT16_MAX - 1) / painter->texture_height;
        uint16_t s2 = ((is1 + FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE) *
                       (UINT16_MAX - 1) / painter->texture_width);
        uint16_t t2 = ((it1 + FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE * height) *
                       (UINT16_MAX - 1) / painter->texture_height);

        v[0].s = s1;
        v[0].t = t2;
        v[1].s = s2;
        v[1].t = t2;
        v[2].s = s1;
        v[2].t = t1;
        v[3].s = s2;
        v[3].t = t1;
}

static void
set_normals(struct fv_vertex_map *v,
            int8_t value)
{
        int i;

        for (i = 0; i < 4; i++)
                v[i].normal = value;
}

static void
generate_square(struct fv_map_painter *painter,
                struct tile_data *data,
                int x, int y)
{
        fv_map_block_t block = fv_map[y * FV_MAP_WIDTH + x];
        struct fv_vertex_map *v;
        int i;
        int z, oz;

        v = reserve_quad(data);

        z = get_block_height(block);

        set_tex_coords_for_image(painter, v,
                                 FV_MAP_GET_BLOCK_TOP_IMAGE(block),
                                 1.0f);
        set_normals(v, FV_MAP_PAINTER_NORMAL_UP);

        for (i = 0; i < 4; i++)
                v[i].z = z;

        v->x = x;
        v->y = y;
        v++;
        v->x = x + 1;
        v->y = y;
        v++;
        v->x = x;
        v->y = y + 1;
        v++;
        v->x = x + 1;
        v->y = y + 1;

        /* Add the side walls */
        if (z > (oz = get_position_height(x, y + 1))) {
                v = add_horizontal_side(data, y + 1, x + 1, oz, x, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_NORTH);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_NORTH_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(x, y - 1))) {
                v = add_horizontal_side(data, y, x, oz, x + 1, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_SOUTH);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_SOUTH_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(x - 1, y))) {
                v = add_vertical_side(data, x, y + 1, oz, y, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_WEST);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_WEST_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(x + 1, y))) {
                v = add_vertical_side(data, x + 1, y, oz, y + 1, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_EAST);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_EAST_IMAGE(block),
                                         z - oz);
        }
}

static void
generate_tile(struct fv_map_painter *painter,
              struct tile_data *data,
              int tx, int ty)
{
        int x, y;

        for (y = 0; y < FV_MAP_TILE_HEIGHT; y++) {
                for (x = 0; x < FV_MAP_TILE_WIDTH; x++) {
                        generate_square(painter,
                                        data,
                                        tx * FV_MAP_TILE_WIDTH + x,
                                        ty * FV_MAP_TILE_HEIGHT + y);
                }
        }

}

static bool
create_texture(struct fv_map_painter *painter,
               const struct fv_image_data *image_data)
{
        VkResult res;

        fv_image_data_get_size(image_data,
                               FV_IMAGE_DATA_MAP_TEXTURE,
                               &painter->texture_width,
                               &painter->texture_height);
        res = fv_image_data_create_image_2d(image_data,
                                            FV_IMAGE_DATA_MAP_TEXTURE,
                                            &painter->texture_image,
                                            &painter->texture_memory);
        if (res != VK_SUCCESS)
                return false;

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = painter->texture_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = fv_image_data_get_format(image_data,
                                                   FV_IMAGE_DATA_MAP_TEXTURE),
                .components = {
                        .r = VK_COMPONENT_SWIZZLE_R,
                        .g = VK_COMPONENT_SWIZZLE_G,
                        .b = VK_COMPONENT_SWIZZLE_B,
                        .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount =
                        fv_image_data_get_miplevels(image_data,
                                                    FV_IMAGE_DATA_MAP_TEXTURE),
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = fv_vk.vkCreateImageView(painter->vk_data->device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &painter->texture_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating image view");
                fv_vk.vkDestroyImage(painter->vk_data->device,
                                     painter->texture_image,
                                     NULL /* allocator */);
                return false;
        }

        return true;
}

static bool
create_descriptor_set(struct fv_map_painter *painter,
                      const struct fv_pipeline_data *pipeline_data)
{
        VkResult res;

        VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = painter->vk_data->descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = (pipeline_data->dsls +
                                FV_PIPELINE_DATA_DSL_TEXTURE_MIPMAP)
        };
        res = fv_vk.vkAllocateDescriptorSets(painter->vk_data->device,
                                             &descriptor_set_allocate_info,
                                             &painter->descriptor_set);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating descriptor set");
                return false;
        }

        VkDescriptorImageInfo descriptor_image_info = {
                .imageView = painter->texture_view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkWriteDescriptorSet write_descriptor_set = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = painter->descriptor_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &descriptor_image_info
        };
        fv_vk.vkUpdateDescriptorSets(painter->vk_data->device,
                                     1, /* descriptorWriteCount */
                                     &write_descriptor_set,
                                     0, /* descriptorCopyCount */
                                     NULL /* pDescriptorCopies */);

        return true;
}

static bool
load_models(struct fv_map_painter *painter)
{
        struct fv_map_painter_special *special;
        bool res;
        int i;

        for (i = 0; i < FV_MAP_PAINTER_N_MODELS; i++) {
                special = painter->specials + i;

                res = fv_model_load(painter->vk_data,
                                    &special->model,
                                    fv_map_painter_models[i].filename);
                if (!res)
                        goto error;
        }

        return true;

error:
        while (--i >= 0)
                fv_model_destroy(painter->vk_data,
                                 &painter->specials[i].model);

        return false;
}

static void
destroy_models(struct fv_map_painter *painter)
{
        int i;

        for (i = 0; i < FV_MAP_PAINTER_N_MODELS; i++) {
                fv_model_destroy(painter->vk_data,
                                 &painter->specials[i].model);
        }
}

struct fv_map_painter *
fv_map_painter_new(const struct fv_vk_data *vk_data,
                   const struct fv_pipeline_data *pipeline_data,
                   const struct fv_image_data *image_data)
{
        struct fv_map_painter *painter;
        struct tile_data data;
        struct fv_map_painter_tile *tile;
        void *memory_map;
        int tx, ty;
        int map_memory_type_index;
        VkResult res;

        painter = fv_alloc(sizeof *painter);

        painter->vk_data = vk_data;
        painter->map_pipeline =
                pipeline_data->pipelines[FV_PIPELINE_DATA_PIPELINE_MAP];
        painter->map_layout =
                pipeline_data->layouts[FV_PIPELINE_DATA_LAYOUT_MAP];
        painter->color_pipeline =
                pipeline_data->pipelines
                [FV_PIPELINE_DATA_PIPELINE_SPECIAL_COLOR];

        fv_list_init(&painter->instance_buffers);
        fv_list_init(&painter->in_use_instance_buffers);
        painter->instance_buffer_map = NULL;

        if (!create_texture(painter, image_data))
                goto error;

        if (!create_descriptor_set(painter, pipeline_data))
                goto error_image;

        if (!load_models(painter))
                goto error_descriptor_set;

        fv_buffer_init(&data.vertices);
        fv_buffer_init(&data.indices);

        tile = painter->tiles;

        for (ty = 0; ty < FV_MAP_TILES_Y; ty++) {
                for (tx = 0; tx < FV_MAP_TILES_X; tx++) {
                        tile->offset = data.indices.length / sizeof (uint16_t);
                        generate_tile(painter, &data, tx, ty);
                        tile->count = (data.indices.length /
                                       sizeof (uint16_t) -
                                       tile->offset);
                        tile++;
                }
        }

        assert(data.vertices.length / sizeof (struct fv_vertex_map) < 65536);

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = data.indices.length + data.vertices.length,
                .usage = (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = fv_vk.vkCreateBuffer(vk_data->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &painter->map_buffer);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating map buffer");
                goto error_buffers;
        }

        res = fv_allocate_store_buffer(vk_data,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &painter->map_buffer,
                                       &painter->map_memory,
                                       &map_memory_type_index,
                                       NULL /* offsets */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating map memory");
                goto error_buffer;
        }

        res = fv_vk.vkMapMemory(vk_data->device,
                                painter->map_memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                &memory_map);
        if (res != VK_SUCCESS) {
                fv_error_message("Error mapping map memory");
                goto error_memory;
        }
        memcpy(memory_map, data.indices.data, data.indices.length);
        memcpy((uint8_t *) memory_map + data.indices.length,
               data.vertices.data,
               data.vertices.length);
        fv_flush_memory(vk_data,
                        map_memory_type_index,
                        painter->map_memory,
                        VK_WHOLE_SIZE);
        fv_vk.vkUnmapMemory(vk_data->device, painter->map_memory);

        painter->vertices_offset = data.indices.length;

        fv_buffer_destroy(&data.indices);
        fv_buffer_destroy(&data.vertices);

        return painter;

error_memory:
        fv_vk.vkFreeMemory(vk_data->device,
                           painter->map_memory,
                           NULL /* allocator */);
error_buffer:
        fv_vk.vkDestroyBuffer(vk_data->device,
                              painter->map_buffer,
                              NULL /* allocator */);
error_buffers:
        fv_buffer_destroy(&data.indices);
        fv_buffer_destroy(&data.vertices);
        destroy_models(painter);
error_descriptor_set:
        fv_vk.vkFreeDescriptorSets(painter->vk_data->device,
                                   painter->vk_data->descriptor_pool,
                                   1, /* descriptorSetCount */
                                   &painter->descriptor_set);
error_image:
        fv_vk.vkFreeMemory(painter->vk_data->device,
                           painter->texture_memory,
                           NULL /* allocator */);
        fv_vk.vkDestroyImage(painter->vk_data->device,
                              painter->texture_image,
                              NULL /* allocator */);
error:
        fv_free(painter);

        return NULL;
}

static void
unmap_instance_buffer(struct fv_map_painter *painter)
{
        struct instance_buffer *buffer;

        if (painter->instance_buffer_map == NULL)
                return;

        buffer = fv_container_of(painter->in_use_instance_buffers.next,
                                 struct instance_buffer,
                                 link);

        fv_flush_memory(painter->vk_data,
                        buffer->memory_type_index,
                        buffer->memory,
                        buffer->watermark);
        fv_vk.vkUnmapMemory(painter->vk_data->device, buffer->memory);
        painter->instance_buffer_map = NULL;
}

static void
flush_specials(struct fv_map_painter *painter,
               VkCommandBuffer command_buffer)
{
        const struct fv_map_painter_special *special;
        struct instance_buffer *instance_buffer;

        if (painter->n_instances == 0)
                return;

        special = painter->specials + painter->current_special;
        instance_buffer = fv_container_of(painter->in_use_instance_buffers.next,
                                          struct instance_buffer,
                                          link);

        fv_vk.vkCmdBindPipeline(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                painter->color_pipeline);
        fv_vk.vkCmdBindVertexBuffers(command_buffer,
                                     0, /* firstBinding */
                                     2, /* bindingCount */
                                     (VkBuffer[]) {
                                             special->model.buffer,
                                             instance_buffer->buffer,
                                     },
                                     (VkDeviceSize[]) {
                                             special->model.vertices_offset,
                                             painter->instance_buffer_offset *
                                             sizeof (struct fv_instance_special)
                                     });
        fv_vk.vkCmdBindIndexBuffer(command_buffer,
                                   special->model.buffer,
                                   special->model.indices_offset,
                                   VK_INDEX_TYPE_UINT16);
        fv_vk.vkCmdDrawIndexed(command_buffer,
                               special->model.n_indices,
                               painter->n_instances,
                               0, /* firstIndex */
                               0, /* vertexOffset */
                               0 /* firstInstance */);

        instance_buffer->watermark =
                (painter->instance_buffer_offset + painter->n_instances) *
                sizeof (struct fv_instance_special);
        painter->instance_buffer_offset += painter->n_instances;
        painter->n_instances = 0;
}

static struct instance_buffer *
create_instance_buffer(struct fv_map_painter *painter)
{
        struct instance_buffer *instance_buffer;
        int memory_type_index;
        VkBuffer buffer;
        VkDeviceMemory memory;
        VkResult res;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = (sizeof (struct fv_instance_special) *
                         FV_MAP_PAINTER_INSTANCES_PER_BUFFER),
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = fv_vk.vkCreateBuffer(painter->vk_data->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &buffer);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating instance buffer");
                return NULL;
        }

        res = fv_allocate_store_buffer(painter->vk_data,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &buffer,
                                       &memory,
                                       &memory_type_index,
                                       NULL /* offsets */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating map memory");
                fv_vk.vkDestroyBuffer(painter->vk_data->device,
                                      buffer,
                                      NULL /* allocator */);
                return NULL;
        }

        instance_buffer = fv_alloc(sizeof *instance_buffer);
        instance_buffer->buffer = buffer;
        instance_buffer->memory = memory;
        instance_buffer->memory_type_index = memory_type_index;

        return instance_buffer;
}

static bool
start_special(struct fv_map_painter *painter,
              VkCommandBuffer command_buffer)
{
        struct instance_buffer *buffer;
        VkResult res;

        if (painter->instance_buffer_map != NULL &&
            painter->n_instances + painter->instance_buffer_offset <
            FV_MAP_PAINTER_INSTANCES_PER_BUFFER)
                return true;

        flush_specials(painter, command_buffer);
        unmap_instance_buffer(painter);
        painter->instance_buffer_offset = 0;

        if (fv_list_empty(&painter->instance_buffers)) {
                buffer = create_instance_buffer(painter);
                if (buffer == NULL)
                        return false;
        } else {
                buffer = fv_container_of(painter->instance_buffers.next,
                                         struct instance_buffer,
                                         link);
                fv_list_remove(&buffer->link);
        }

        res = fv_vk.vkMapMemory(painter->vk_data->device,
                                buffer->memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                (void **) &painter->instance_buffer_map);
        if (res != VK_SUCCESS) {
                fv_error_message("Error mapping instance memory");
                painter->instance_buffer_map = NULL;
                fv_list_insert(&painter->instance_buffers, &buffer->link);
                return false;
        }

        buffer->watermark = 0;

        fv_list_insert(&painter->in_use_instance_buffers, &buffer->link);

        return true;
}

static void
paint_special(struct fv_map_painter *painter,
              const struct fv_map_special *special,
              VkCommandBuffer command_buffer,
              const struct fv_transform *transform_in)
{
        struct fv_transform transform = *transform_in;
        struct fv_instance_special *instance;

        if (painter->current_special != special->num)
                flush_specials(painter, command_buffer);

        if (!start_special(painter, command_buffer))
                return;

        fv_matrix_translate(&transform.modelview,
                            special->x + 0.5f,
                            special->y + 0.5f,
                            0.0f);
        if (special->rotation != 0)
                fv_matrix_rotate(&transform.modelview,
                                 special->rotation * 360.0f /
                                 (UINT16_MAX + 1.0f),
                                 0.0f, 0.0f, 1.0f);

        fv_transform_dirty(&transform);
        fv_transform_ensure_mvp(&transform);
        fv_transform_ensure_normal_transform(&transform);

        painter->current_special = special->num;
        instance = (painter->instance_buffer_map +
                    painter->instance_buffer_offset +
                    painter->n_instances);
        memcpy(instance->modelview,
               &transform.mvp.xx,
               sizeof instance->modelview);
        memcpy(instance->normal_transform,
               transform.normal_transform,
               sizeof instance->normal_transform);

        painter->n_instances++;
}

void
fv_map_painter_begin_frame(struct fv_map_painter *painter)
{
        painter->instance_buffer_offset = 0;
        fv_list_insert_list(&painter->instance_buffers,
                            &painter->in_use_instance_buffers);
        fv_list_init(&painter->in_use_instance_buffers);
}

void
fv_map_painter_paint(struct fv_map_painter *painter,
                     struct fv_logic *logic,
                     VkCommandBuffer command_buffer,
                     struct fv_paint_state *paint_state)
{
        int x_min, x_max, y_min, y_max;
        const struct fv_map_tile *map_tile;
        const struct fv_map_painter_tile *tile = NULL;
        int count;
        int y, x, i;
        float normal_transform[12];

        x_min = floorf((paint_state->center_x - paint_state->visible_w / 2.0f) /
                       FV_MAP_TILE_WIDTH);
        x_max = ceilf((paint_state->center_x + paint_state->visible_w / 2.0f) /
                      FV_MAP_TILE_WIDTH);
        y_min = floorf((paint_state->center_y - paint_state->visible_h / 2.0f) /
                       FV_MAP_TILE_HEIGHT);
        y_max = ceilf((paint_state->center_y + paint_state->visible_h / 2.0f) /
                      FV_MAP_TILE_HEIGHT);

        if (x_min < 0)
                x_min = 0;
        if (x_max > FV_MAP_TILES_X)
                x_max = FV_MAP_TILES_X;
        if (y_min < 0)
                y_min = 0;
        if (y_max > FV_MAP_TILES_Y)
                y_max = FV_MAP_TILES_Y;

        if (y_min >= y_max || x_min >= x_max)
                return;

        painter->n_instances = 0;
        painter->current_special = 0;

        for (y = y_min; y < y_max; y++) {
                for (x = x_max - 1; x >= x_min; x--) {
                        map_tile = fv_map_tiles + y * FV_MAP_TILES_X + x;
                        for (i = 0; i < map_tile->n_specials; i++) {
                                paint_special(painter,
                                              map_tile->specials + i,
                                              command_buffer,
                                              &paint_state->transform);
                        }
                }
        }

        flush_specials(painter, command_buffer);

        fv_transform_ensure_mvp(&paint_state->transform);
        fv_transform_ensure_normal_transform(&paint_state->transform);

        for (i = 0; i < 3; i++) {
                memcpy(normal_transform + i * 4,
                       paint_state->transform.normal_transform + i * 3,
                       sizeof (float) * 3);
                normal_transform[i * 4 + 3] = 0.0f;
        }

        fv_vk.vkCmdBindPipeline(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                painter->map_pipeline);
        fv_vk.vkCmdBindDescriptorSets(command_buffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      painter->map_layout,
                                      0, /* firstSet */
                                      1, /* descriptorSetCount */
                                      &painter->descriptor_set,
                                      0, /* dynamicOffsetCount */
                                      NULL /* pDynamicOffsets */);
        fv_vk.vkCmdPushConstants(command_buffer,
                                 painter->map_layout,
                                 VK_SHADER_STAGE_VERTEX_BIT,
                                 offsetof(struct fv_vertex_map_push_constants,
                                          transform),
                                 sizeof (float) * 16,
                                 &paint_state->transform.mvp.xx);
        fv_vk.vkCmdPushConstants(command_buffer,
                                 painter->map_layout,
                                 VK_SHADER_STAGE_VERTEX_BIT,
                                 offsetof(struct fv_vertex_map_push_constants,
                                          normal_transform),
                                 sizeof (float) * 12,
                                 normal_transform);
        fv_vk.vkCmdBindVertexBuffers(command_buffer,
                                     0, /* firstBinding */
                                     1, /* bindingCount */
                                     &painter->map_buffer,
                                     &painter->vertices_offset);
        fv_vk.vkCmdBindIndexBuffer(command_buffer,
                                   painter->map_buffer,
                                   0, /* offset */
                                   VK_INDEX_TYPE_UINT16);

        for (y = y_min; y < y_max; y++) {
                count = 0;

                for (x = x_max - 1; x >= x_min; x--) {
                        tile = painter->tiles +
                                y * FV_MAP_TILES_X + x;
                        count += tile->count;
                }

                fv_vk.vkCmdDrawIndexed(command_buffer,
                                       count,
                                       1, /* instanceCount */
                                       tile->offset,
                                       0, /* vertexOffset */
                                       0 /* firstInstance */);
        }
}

void
fv_map_painter_end_frame(struct fv_map_painter *painter)
{
        unmap_instance_buffer(painter);
}

static void
free_instance_buffers(struct fv_map_painter *painter,
                      struct fv_list *buffers)
{
        struct instance_buffer *buffer, *tmp;

        fv_list_for_each_safe(buffer, tmp, buffers, link) {
                fv_vk.vkFreeMemory(painter->vk_data->device,
                                   buffer->memory,
                                   NULL /* allocator */);
                fv_vk.vkDestroyBuffer(painter->vk_data->device,
                                      buffer->buffer,
                                      NULL /* allocator */);
                fv_free(buffer);
        }
}

void
fv_map_painter_free(struct fv_map_painter *painter)
{
        free_instance_buffers(painter, &painter->instance_buffers);
        free_instance_buffers(painter, &painter->in_use_instance_buffers);

        fv_vk.vkFreeMemory(painter->vk_data->device,
                           painter->map_memory,
                           NULL /* allocator */);
        fv_vk.vkDestroyBuffer(painter->vk_data->device,
                              painter->map_buffer,
                              NULL /* allocator */);
        fv_vk.vkFreeDescriptorSets(painter->vk_data->device,
                                   painter->vk_data->descriptor_pool,
                                   1, /* descriptorSetCount */
                                   &painter->descriptor_set);
        fv_vk.vkFreeMemory(painter->vk_data->device,
                           painter->texture_memory,
                           NULL /* allocator */);
        fv_vk.vkDestroyImageView(painter->vk_data->device,
                                 painter->texture_view,
                                 NULL /* allocator */);
        fv_vk.vkDestroyImage(painter->vk_data->device,
                              painter->texture_image,
                              NULL /* allocator */);
        destroy_models(painter);

        fv_free(painter);
}
