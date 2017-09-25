/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2015, 2016 Neil Roberts
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

#define FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE 64

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

struct fv_map_painter_tile {
        int offset;
        int count;
};

struct fv_map_painter {
        struct fv_map_painter_tile tiles[FV_MAP_TILES_X *
                                         FV_MAP_TILES_Y];

        VkDevice device;
        VkPipeline map_pipeline;
        VkPipelineLayout map_layout;
        VkBuffer map_buffer;
        VkDeviceMemory map_memory;

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

static VkResult
allocate_buffer_memory(const struct fv_pipeline_data *pipeline_data,
                       VkBuffer buffer,
                       VkDeviceMemory *memory_out)
{
        VkDeviceMemory memory;
        const VkMemoryType *memory_types =
                pipeline_data->memory_properties.memoryTypes;
        VkMemoryRequirements reqs;
        VkResult res;
        int memory_type_index;
        int i;

        fv_vk.vkGetBufferMemoryRequirements(pipeline_data->device,
                                            buffer,
                                            &reqs);

        for (i = 0; i < pipeline_data->memory_properties.memoryTypeCount; i++) {
                if ((memory_types[i].propertyFlags & reqs.memoryTypeBits) ==
                    reqs.memoryTypeBits) {
                        memory_type_index = i;
                        goto found_memory_type;
                }
        }

        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
found_memory_type: (void) 0;

        VkMemoryAllocateInfo allocate_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = reqs.size,
                .memoryTypeIndex = memory_type_index
        };
        res = fv_vk.vkAllocateMemory(pipeline_data->device,
                                     &allocate_info,
                                     NULL, /* allocator */
                                     &memory);
        if (res != VK_SUCCESS)
                return res;

        fv_vk.vkBindBufferMemory(pipeline_data->device,
                                 buffer,
                                 memory,
                                 0 /* offset */);
        *memory_out = memory;

        return VK_SUCCESS;
}

struct fv_map_painter *
fv_map_painter_new(struct fv_pipeline_data *pipeline_data)
{
        struct fv_map_painter *painter;
        struct tile_data data;
        struct fv_map_painter_tile *tile;
        void *memory_map;
        int tx, ty;
        VkResult res;

        painter = fv_alloc(sizeof *painter);

        painter->device = pipeline_data->device;
        painter->map_pipeline = pipeline_data->map_pipeline;
        painter->map_layout = pipeline_data->layout;

        /* STUB */
        painter->texture_width = 256;
        painter->texture_height = 256;

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
                .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = fv_vk.vkCreateBuffer(painter->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &painter->map_buffer);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating map buffer");
                goto error_buffers;
        }

        res = allocate_buffer_memory(pipeline_data,
                                     painter->map_buffer,
                                     &painter->map_memory);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating map memory");
                goto error_buffer;
        }

        res = fv_vk.vkMapMemory(painter->device,
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
        fv_vk.vkUnmapMemory(painter->device, painter->map_memory);

        painter->vertices_offset = data.indices.length;

        fv_buffer_destroy(&data.indices);
        fv_buffer_destroy(&data.vertices);

        return painter;

error_memory:
        fv_vk.vkFreeMemory(painter->device,
                           painter->map_memory,
                           NULL /* allocator */);
error_buffer:
        fv_vk.vkDestroyBuffer(painter->device,
                              painter->map_buffer,
                              NULL /* allocator */);
error_buffers:
        fv_buffer_destroy(&data.indices);
        fv_buffer_destroy(&data.vertices);

        fv_free(painter);

        return NULL;
}

void
fv_map_painter_paint(struct fv_map_painter *painter,
                     struct fv_logic *logic,
                     VkCommandBuffer command_buffer,
                     struct fv_paint_state *paint_state)
{
        int x_min, x_max, y_min, y_max;
        const struct fv_map_painter_tile *tile = NULL;
        int count;
        int y, x;

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

        fv_transform_ensure_mvp(&paint_state->transform);
        fv_transform_ensure_normal_transform(&paint_state->transform);

        fv_vk.vkCmdBindPipeline(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                painter->map_pipeline);
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
                                 sizeof (float) * 9,
                                 paint_state->transform.normal_transform);
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
fv_map_painter_free(struct fv_map_painter *painter)
{
        fv_vk.vkFreeMemory(painter->device,
                           painter->map_memory,
                           NULL /* allocator */);
        fv_vk.vkDestroyBuffer(painter->device,
                              painter->map_buffer,
                              NULL /* allocator */);

        fv_free(painter);
}
