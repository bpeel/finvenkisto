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

#include "fv-circle-painter.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-error-message.h"
#include "fv-list.h"
#include "fv-vertex.h"
#include "fv-allocate-store.h"
#include "fv-flush-memory.h"

struct fv_circle_painter {
        const struct fv_vk_data *vk_data;

        VkPipeline pipeline;
        VkPipelineLayout layout;

        VkBuffer circle_buffer;
        VkDeviceMemory circle_memory;

        struct fv_list instance_buffers;
        struct fv_list in_use_instance_buffers;
        struct fv_instance_circle *instance_buffer_map;
        int buffer_offset;
};

struct instance_buffer {
        struct fv_list link;
        VkBuffer buffer;
        VkDeviceMemory memory;
        VkDeviceSize watermark;
        int memory_type_index;
};

#define FV_CIRCLE_PAINTER_INSTANCES_PER_BUFFER \
        (4096 / sizeof (struct fv_instance_circle))

#define FV_CIRCLE_PAINTER_N_VERTICES 3

static bool
create_circle(struct fv_circle_painter *painter)
{
        const struct fv_vk_data *vk_data = painter->vk_data;
        struct fv_vertex_circle *vertices;
        VkResult res;
        int memory_type_index;
        int i;
        float angle;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = (FV_CIRCLE_PAINTER_N_VERTICES *
                         sizeof (struct fv_vertex_circle)),
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = fv_vk.vkCreateBuffer(vk_data->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &painter->circle_buffer);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating cirlce buffer");
                painter->circle_buffer = NULL;
                return false;
        }

        res = fv_allocate_store_buffer(vk_data,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &painter->circle_buffer,
                                       &painter->circle_memory,
                                       &memory_type_index,
                                       NULL /* offsets */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating cirlce memory");
                painter->circle_memory = NULL;
                return false;
        }

        res = fv_vk.vkMapMemory(vk_data->device,
                                painter->circle_memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                (void **) &vertices);
        if (res != VK_SUCCESS) {
                fv_error_message("Error mapping circle memory");
                return false;
        }

        for (i = 0; i < FV_CIRCLE_PAINTER_N_VERTICES; i++) {
                angle = 2.0 * M_PI * i / FV_CIRCLE_PAINTER_N_VERTICES;
                vertices[i].x = sinf(angle);
                vertices[i].y = cosf(angle);
        }

        fv_flush_memory(vk_data,
                        memory_type_index,
                        painter->circle_memory,
                        VK_WHOLE_SIZE);
        fv_vk.vkUnmapMemory(vk_data->device, painter->circle_memory);

        return true;
}

struct fv_circle_painter *
fv_circle_painter_new(const struct fv_vk_data *vk_data,
                      const struct fv_pipeline_data *pipeline_data)
{
        struct fv_circle_painter *painter = fv_calloc(sizeof *painter);

        painter->vk_data = vk_data;
        painter->pipeline =
                pipeline_data->pipelines[FV_PIPELINE_DATA_PIPELINE_CIRCLE];
        fv_list_init(&painter->instance_buffers);
        fv_list_init(&painter->in_use_instance_buffers);

        if (!create_circle(painter))
                goto error;

        return painter;

error:
        fv_circle_painter_free(painter);
        return NULL;
}

struct paint_closure {
        struct fv_circle_painter *painter;

        VkCommandBuffer command_buffer;
        int n_instances;
};

static void
unmap_buffer(struct fv_circle_painter *painter)
{
        struct instance_buffer *instance_buffer;

        if (painter->instance_buffer_map == NULL)
                return;

        instance_buffer = fv_container_of(painter->in_use_instance_buffers.next,
                                          struct instance_buffer,
                                          link);
        fv_flush_memory(painter->vk_data,
                        instance_buffer->memory_type_index,
                        instance_buffer->memory,
                        instance_buffer->watermark);
        fv_vk.vkUnmapMemory(painter->vk_data->device, instance_buffer->memory);
        painter->instance_buffer_map = NULL;
}

static void
flush_circles(struct paint_closure *data)
{
        struct fv_circle_painter *painter = data->painter;
        struct instance_buffer *instance_buffer;

        if (data->n_instances == 0)
                return;

        instance_buffer = fv_container_of(painter->in_use_instance_buffers.next,
                                          struct instance_buffer,
                                          link);

        fv_vk.vkCmdBindPipeline(data->command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                painter->pipeline);
        fv_vk.vkCmdBindVertexBuffers(data->command_buffer,
                                     0, /* firstBinding */
                                     2, /* bindingCount */
                                     (VkBuffer[]) {
                                             painter->circle_buffer,
                                             instance_buffer->buffer,
                                     },
                                     (VkDeviceSize[]) {
                                             0,
                                             painter->buffer_offset *
                                             sizeof (struct fv_instance_circle),
                                     });
        fv_vk.vkCmdDraw(data->command_buffer,
                        FV_CIRCLE_PAINTER_N_VERTICES,
                        data->n_instances,
                        0, /* firstVertex */
                        0 /* firstInstance */);

        instance_buffer->watermark =
                (painter->buffer_offset + data->n_instances) *
                sizeof (struct fv_instance_circle);
        painter->buffer_offset += data->n_instances;
        data->n_instances = 0;
}

static struct instance_buffer *
create_instance_buffer(struct fv_circle_painter *painter)
{
        struct instance_buffer *instance_buffer;
        VkBuffer buffer;
        VkDeviceMemory memory;
        int memory_type_index;
        VkResult res;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = (sizeof (struct fv_instance_circle) *
                         FV_CIRCLE_PAINTER_INSTANCES_PER_BUFFER),
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
                                       NULL);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating instance memory");
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
start_instance(struct paint_closure *data)
{
        struct fv_circle_painter *painter = data->painter;
        struct instance_buffer *buffer;
        VkResult res;

        if (painter->buffer_offset + data->n_instances >=
            FV_CIRCLE_PAINTER_INSTANCES_PER_BUFFER)
                flush_circles(data);

        if (painter->buffer_offset < FV_CIRCLE_PAINTER_INSTANCES_PER_BUFFER &&
            painter->instance_buffer_map != NULL)
                return true;

        unmap_buffer(painter);

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
        painter->buffer_offset = 0;

        return true;
}

static void
paint_circle(struct paint_closure *data,
             float x, float y,
             float radius)
{
        struct fv_instance_circle *instance;

        if (!start_instance(data))
                return;

        instance = (data->painter->instance_buffer_map +
                    data->painter->buffer_offset +
                    data->n_instances);
        instance->x = x;
        instance->y = y;
        instance->radius = radius;

        data->n_instances++;
}

void
fv_circle_painter_begin_frame(struct fv_circle_painter *painter)
{
        fv_list_insert_list(&painter->instance_buffers,
                            &painter->in_use_instance_buffers);
        fv_list_init(&painter->in_use_instance_buffers);
}

void
fv_circle_painter_paint(struct fv_circle_painter *painter,
                        struct fv_logic *logic,
                        VkCommandBuffer command_buffer,
                        const struct fv_paint_state *paint_state)
{
        struct paint_closure data;
        unsigned int ticks = fv_logic_get_ticks(logic);
        float radius;
        int x, y;

        data.painter = painter;
        data.n_instances = 0;
        data.command_buffer = command_buffer;

        for (y = 0; y < 2; y++) {
                for (x = 0; x < 2; x++) {
                        radius = ticks % 1000 / 1000.0f;

                        if ((ticks / 1000) & 1)
                                radius = 1.0f - radius;

                        radius /= 2.0f;

                        paint_circle(&data,
                                     x - 0.5f,
                                     y - 0.5f,
                                     radius);

                        ticks += 500;
                }
        }

        flush_circles(&data);
}

void
fv_circle_painter_end_frame(struct fv_circle_painter *painter)
{
        unmap_buffer(painter);
}

static void
free_instance_buffers(struct fv_circle_painter *painter,
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
fv_circle_painter_free(struct fv_circle_painter *painter)
{
        free_instance_buffers(painter, &painter->instance_buffers);
        free_instance_buffers(painter, &painter->in_use_instance_buffers);

        if (painter->circle_memory) {
                fv_vk.vkFreeMemory(painter->vk_data->device,
                                   painter->circle_memory,
                                   NULL /* allocator */);
                painter->circle_memory = NULL;
        }
        if (painter->circle_buffer) {
                fv_vk.vkDestroyBuffer(painter->vk_data->device,
                                      painter->circle_buffer,
                                      NULL /* allocator */);
                painter->circle_buffer = NULL;
        }

        fv_free(painter);
}
