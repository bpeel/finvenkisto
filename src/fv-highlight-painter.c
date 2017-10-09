/*
 * Finvenkisto
 *
 * Copyright (C) 2017 Neil Roberts
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
#include <assert.h>

#include "fv-highlight-painter.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-error-message.h"
#include "fv-vertex.h"
#include "fv-allocate-store.h"
#include "fv-flush-memory.h"
#include "fv-map.h"

#define FV_HIGHLIGHT_PAINTER_MAX_HIGHLIGHTS (FV_MAP_TILES_X + 1 +       \
                                             FV_MAP_TILES_Y + 1 +       \
                                             1)
#define FV_HIGHLIGHT_PAINTER_INDICES_OFFSET             \
        (sizeof (struct fv_vertex_highlight) * 4 *      \
         FV_HIGHLIGHT_PAINTER_MAX_HIGHLIGHTS)

struct fv_highlight_painter {
        const struct fv_vk_data *vk_data;

        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkBuffer vertex_buffer;
        VkDeviceMemory vertex_memory;
        int vertex_memory_type_index;
        VkDeviceSize vertex_memory_watermark;

        struct fv_vertex_highlight *vertex_memory_map;

        /* Count of highlights that have been placed in the buffer since
         * the last frame was started */
        int buffer_offset;
};

static bool
create_buffer(struct fv_highlight_painter *painter)
{
        VkResult res;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = (FV_HIGHLIGHT_PAINTER_INDICES_OFFSET +
                         sizeof (uint16_t) *
                         FV_HIGHLIGHT_PAINTER_MAX_HIGHLIGHTS * 6),
                .usage = (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = fv_vk.vkCreateBuffer(painter->vk_data->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &painter->vertex_buffer);
        if (res != VK_SUCCESS) {
                painter->vertex_buffer = NULL;
                fv_error_message("Error creating highlight vertex buffer");
                return false;
        }

        res = fv_allocate_store_buffer(painter->vk_data,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &painter->vertex_buffer,
                                       &painter->vertex_memory,
                                       &painter->vertex_memory_type_index,
                                       NULL);
        if (res != VK_SUCCESS) {
                painter->vertex_memory = NULL;
                fv_error_message("Error creating highlight vertex memory");
                return false;
        }

        res = fv_vk.vkMapMemory(painter->vk_data->device,
                                painter->vertex_memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                (void **) &painter->vertex_memory_map);
        if (res != VK_SUCCESS) {
                painter->vertex_memory_map = NULL;
                fv_error_message("Error mapping highlight vertex memory");
                return false;
        }

        return true;
}

struct fv_highlight_painter *
fv_highlight_painter_new(const struct fv_vk_data *vk_data,
                         const struct fv_pipeline_data *pipeline_data)
{
        struct fv_highlight_painter *painter = fv_calloc(sizeof *painter);
        uint16_t *indices;
        int i;

        painter->vk_data = vk_data;
        painter->pipeline =
                pipeline_data->pipelines[FV_PIPELINE_DATA_PIPELINE_HIGHLIGHT];
        painter->layout =
                pipeline_data->layouts[FV_PIPELINE_DATA_LAYOUT_SHOUT];

        if (!create_buffer(painter))
                goto error;

        indices = (uint16_t *) ((uint8_t *) painter->vertex_memory_map +
                                FV_HIGHLIGHT_PAINTER_INDICES_OFFSET);

        for (i = 0; i < FV_HIGHLIGHT_PAINTER_MAX_HIGHLIGHTS; i++) {
                indices[i * 6 + 0] = i * 4 + 0;
                indices[i * 6 + 1] = i * 4 + 1;
                indices[i * 6 + 2] = i * 4 + 2;
                indices[i * 6 + 3] = i * 4 + 2;
                indices[i * 6 + 4] = i * 4 + 1;
                indices[i * 6 + 5] = i * 4 + 3;
        }

        fv_flush_memory(painter->vk_data,
                        painter->vertex_memory_type_index,
                        painter->vertex_memory,
                        VK_WHOLE_SIZE);

        return painter;

error:
        fv_highlight_painter_free(painter);
        return NULL;
}

static void
set_highlight(struct fv_vertex_highlight *vertex,
              const struct fv_highlight_painter_highlight *highlight)
{
        int i;

        for (i = 0; i < 4; i++) {
                vertex[i].z = highlight->z;
                vertex[i].r = highlight->r;
                vertex[i].g = highlight->g;
                vertex[i].b = highlight->b;
                vertex[i].a = highlight->a;
        }

        vertex->x = highlight->x;
        vertex->y = highlight->y;
        vertex++;

        vertex->x = highlight->x + highlight->w;
        vertex->y = highlight->y;
        vertex++;

        vertex->x = highlight->x;
        vertex->y = highlight->y + highlight->h;
        vertex++;

        vertex->x = highlight->x + highlight->w;
        vertex->y = highlight->y + highlight->h;
        vertex++;
}

void
fv_highlight_painter_paint(struct fv_highlight_painter *painter,
                           VkCommandBuffer command_buffer,
                           size_t n_highlights,
                           const struct fv_highlight_painter_highlight *
                           highlights,
                           struct fv_paint_state *paint_state)
{
        struct fv_vertex_highlight *vertex;
        VkDeviceSize vertices_offset = 0;
        size_t i;

        assert(painter->buffer_offset + n_highlights <=
               FV_HIGHLIGHT_PAINTER_MAX_HIGHLIGHTS);

        vertex = painter->vertex_memory_map + painter->buffer_offset * 4;

        for (i = 0; i < n_highlights; i++) {
                set_highlight(vertex, highlights + i);
                vertex += 4;
        }

        fv_transform_ensure_mvp(&paint_state->transform);

        fv_vk.vkCmdBindPipeline(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                painter->pipeline);
        fv_vk.vkCmdPushConstants(command_buffer,
                                 painter->layout,
                                 VK_SHADER_STAGE_VERTEX_BIT,
                                 offsetof(struct fv_vertex_shout_push_constants,
                                          transform),
                                 sizeof (float) * 16,
                                 &paint_state->transform.mvp.xx);
         fv_vk.vkCmdBindVertexBuffers(command_buffer,
                                     0, /* firstBinding */
                                     1, /* bindingCount */
                                     &painter->vertex_buffer,
                                     &vertices_offset);
        fv_vk.vkCmdBindIndexBuffer(command_buffer,
                                   painter->vertex_buffer,
                                   FV_HIGHLIGHT_PAINTER_INDICES_OFFSET,
                                   VK_INDEX_TYPE_UINT16);
        fv_vk.vkCmdDrawIndexed(command_buffer,
                               n_highlights * 6,
                               1, /* instanceCount */
                               0, /* firstIndex */
                               0, /* vertexOffset */
                               0 /* firstInstance */);

        painter->buffer_offset += n_highlights;
        painter->vertex_memory_watermark =
                painter->buffer_offset *
                sizeof (struct fv_vertex_highlight) * 4;
}

void
fv_highlight_painter_begin_frame(struct fv_highlight_painter *painter)
{
        painter->buffer_offset = 0;
        painter->vertex_memory_watermark = 0;
}

void
fv_highlight_painter_end_frame(struct fv_highlight_painter *painter)
{
        fv_flush_memory(painter->vk_data,
                        painter->vertex_memory_type_index,
                        painter->vertex_memory,
                        painter->vertex_memory_watermark);
}

void
fv_highlight_painter_free(struct fv_highlight_painter *painter)
{
        if (painter->vertex_memory_map) {
                fv_vk.vkUnmapMemory(painter->vk_data->device,
                                    painter->vertex_memory);
        }

        if (painter->vertex_memory) {
                fv_vk.vkFreeMemory(painter->vk_data->device,
                                   painter->vertex_memory,
                                   NULL /* allocator */);
        }
        if (painter->vertex_buffer) {
                fv_vk.vkDestroyBuffer(painter->vk_data->device,
                                      painter->vertex_buffer,
                                      NULL /* allocator */);
        }

        fv_free(painter);
}