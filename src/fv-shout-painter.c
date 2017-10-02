/*
 * Finvenkisto
 *
 * Copyright (C) 2015, 2017 Neil Roberts
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

#include "fv-shout-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-error-message.h"
#include "fv-vertex.h"
#include "fv-allocate-store.h"

/* If all of the players are shouting at the same time then a shout
 * will be painted for each of them in each screen, so we need to be
 * able to paint n_players^2 shouts */
#define FV_SHOUT_PAINTER_MAX_SHOUTS (FV_LOGIC_MAX_PLAYERS * \
                                     FV_LOGIC_MAX_PLAYERS)

struct fv_shout_painter {
        const struct fv_vk_data *vk_data;

        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkImage texture_image;
        VkDeviceMemory texture_memory;
        VkImageView texture_view;
        VkDescriptorSet descriptor_set;
        VkBuffer vertex_buffer;
        VkDeviceMemory vertex_memory;

        struct fv_vertex_shout *vertex_memory_map;

        /* Count of shouts that have been placed in the buffer since
         * the last frame was started */
        int buffer_offset;
};

static bool
create_texture(struct fv_shout_painter *painter,
               const struct fv_image_data *image_data)
{
        VkResult res;

        res = fv_image_data_create_image_2d(image_data,
                                            FV_IMAGE_DATA_NEKROKODILU,
                                            &painter->texture_image,
                                            &painter->texture_memory);
        if (res != VK_SUCCESS) {
                painter->texture_image = NULL;
                painter->texture_memory = NULL;
                fv_error_message("Error creating shout texture");
                return false;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = painter->texture_image,
                .viewType =  VK_IMAGE_VIEW_TYPE_2D,
                .format = fv_image_data_get_format(image_data,
                                                   FV_IMAGE_DATA_NEKROKODILU),
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
                                                    FV_IMAGE_DATA_NEKROKODILU),
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = fv_vk.vkCreateImageView(painter->vk_data->device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &painter->texture_view);
        if (res != VK_SUCCESS) {
                painter->texture_view = NULL;
                fv_error_message("Error creating image view");
                return false;
        }

        return true;
}

static bool
create_descriptor_set(struct fv_shout_painter *painter,
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
                painter->descriptor_set = NULL;
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
create_buffer(struct fv_shout_painter *painter)
{
        VkResult res;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = (sizeof (struct fv_vertex_shout) *
                         FV_SHOUT_PAINTER_MAX_SHOUTS * 3),
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = fv_vk.vkCreateBuffer(painter->vk_data->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &painter->vertex_buffer);
        if (res != VK_SUCCESS) {
                painter->vertex_buffer = NULL;
                fv_error_message("Error creating shout vertex buffer");
                return false;
        }

        res = fv_allocate_store_buffer(painter->vk_data,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &painter->vertex_buffer,
                                       &painter->vertex_memory,
                                       NULL);
        if (res != VK_SUCCESS) {
                painter->vertex_memory = NULL;
                fv_error_message("Error creating shout vertex memory");
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
                fv_error_message("Error mapping shout vertex memory");
                return false;
        }

        return true;
}

struct fv_shout_painter *
fv_shout_painter_new(const struct fv_vk_data *vk_data,
                     const struct fv_pipeline_data *pipeline_data,
                     const struct fv_image_data *image_data)
{
        struct fv_shout_painter *painter = fv_calloc(sizeof *painter);

        painter->vk_data = vk_data;
        painter->pipeline =
                pipeline_data->pipelines[FV_PIPELINE_DATA_PIPELINE_SHOUT];
        painter->layout =
                pipeline_data->layouts[FV_PIPELINE_DATA_LAYOUT_SHOUT];

        if (!create_texture(painter, image_data))
                goto error;

        if (!create_buffer(painter))
                goto error;

        if (!create_descriptor_set(painter, pipeline_data))
                goto error;

        return painter;

error:
        fv_shout_painter_free(painter);
        return NULL;
}

struct paint_closure {
        struct fv_shout_painter *painter;
        int n_shouts;
};

static void
paint_cb(const struct fv_logic_shout *shout,
         void *user_data)
{
        struct paint_closure *data = user_data;
        struct fv_shout_painter *painter = data->painter;
        struct fv_vertex_shout *vertex;
        float cx, cy, ccx, ccy;

        assert(data->n_shouts < FV_LOGIC_MAX_PLAYERS);
        assert(painter->buffer_offset + data->n_shouts <
               FV_SHOUT_PAINTER_MAX_SHOUTS);

        cx = cosf(shout->direction - FV_LOGIC_SHOUT_ANGLE / 2.0f);
        cy = sinf(shout->direction - FV_LOGIC_SHOUT_ANGLE / 2.0f);
        ccx = cosf(shout->direction + FV_LOGIC_SHOUT_ANGLE / 2.0f);
        ccy = sinf(shout->direction + FV_LOGIC_SHOUT_ANGLE / 2.0f);

        vertex = (painter->vertex_memory_map +
                  (painter->buffer_offset + data->n_shouts) * 3);

        vertex->x = shout->x;
        vertex->y = shout->y;
        vertex->z = 1.5f;
        vertex->s = 0.0f;
        vertex->t = 0.5f;
        vertex++;

        vertex->x = shout->x + shout->distance * cx;
        vertex->y = shout->y + shout->distance * cy;
        vertex->z = 1.5f;
        vertex->s = 1.0f;
        vertex->t = cx >= 0.0f;
        vertex++;

        vertex->x = shout->x + shout->distance * ccx;
        vertex->y = shout->y + shout->distance * ccy;
        vertex->z = 1.5f;
        vertex->s = 1.0f;
        vertex->t = cx < 0.0f;
        vertex++;

        data->n_shouts++;
}

void
fv_shout_painter_begin_frame(struct fv_shout_painter *painter)
{
        painter->buffer_offset = 0;
}

void
fv_shout_painter_paint(struct fv_shout_painter *painter,
                       struct fv_logic *logic,
                       VkCommandBuffer command_buffer,
                       const struct fv_paint_state *paint_state)
{
        struct paint_closure data;
        VkDeviceSize vertices_offset =
                painter->buffer_offset * sizeof (struct fv_vertex_shout) * 3;

        data.painter = painter;
        data.n_shouts = 0;

        fv_logic_for_each_shout(logic, paint_cb, &data);

        if (data.n_shouts <= 0)
                return;

        fv_vk.vkCmdBindPipeline(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                painter->pipeline);
        fv_vk.vkCmdBindDescriptorSets(command_buffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      painter->layout,
                                      0, /* firstSet */
                                      1, /* descriptorSetCount */
                                      &painter->descriptor_set,
                                      0, /* dynamicOffsetCount */
                                      NULL /* pDynamicOffsets */);
        fv_vk.vkCmdBindVertexBuffers(command_buffer,
                                     0, /* firstBinding */
                                     1, /* bindingCount */
                                     &painter->vertex_buffer,
                                     &vertices_offset);
        fv_vk.vkCmdDraw(command_buffer,
                        data.n_shouts * 3,
                        1, /* instanceCount */
                        0, /* firstVertex */
                        0 /* firstInstance */);

        painter->buffer_offset += data.n_shouts;
}

void
fv_shout_painter_end_frame(struct fv_shout_painter *painter)
{
}

void
fv_shout_painter_free(struct fv_shout_painter *painter)
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
        if (painter->descriptor_set) {
                fv_vk.vkFreeDescriptorSets(painter->vk_data->device,
                                           painter->vk_data->descriptor_pool,
                                           1, /* descriptorSetCount */
                                           &painter->descriptor_set);
        }
        if (painter->texture_view) {
                fv_vk.vkDestroyImageView(painter->vk_data->device,
                                         painter->texture_view,
                                         NULL /* allocator */);
        }
        if (painter->texture_image) {
                fv_vk.vkDestroyImage(painter->vk_data->device,
                                     painter->texture_image,
                                     NULL /* allocator */);
        }
        if (painter->texture_memory) {
                fv_vk.vkFreeMemory(painter->vk_data->device,
                                   painter->texture_memory,
                                   NULL /* allocator */);
        }

        fv_free(painter);
}
