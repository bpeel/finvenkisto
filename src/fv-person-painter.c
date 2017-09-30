/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2017 Neil Roberts
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

#include "fv-person-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-model.h"
#include "fv-error-message.h"
#include "fv-list.h"
#include "fv-vertex.h"
#include "fv-allocate-store.h"

/* Textures to use for the different person types. These must match
 * the order of the enum in fv_person_type */
enum fv_image_data_image
textures[] = {
        FV_IMAGE_DATA_FINVENKISTO,
        FV_IMAGE_DATA_BAMBO1,
        FV_IMAGE_DATA_BAMBO2,
        FV_IMAGE_DATA_BAMBO3,
        FV_IMAGE_DATA_GUFUJESTRO,
        FV_IMAGE_DATA_TOILETGUY,
        FV_IMAGE_DATA_PYJAMAS,
};

struct fv_person_painter {
        const struct fv_vk_data *vk_data;

        struct fv_model model;
        bool model_loaded;

        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkImage texture_image;
        VkDeviceMemory texture_memory;
        VkImageView texture_view;
        VkSampler sampler;
        VkDescriptorSet descriptor_set;

        struct fv_list instance_buffers;
        struct fv_list in_use_instance_buffers;
};

struct instance_buffer {
        struct fv_list link;
        VkBuffer buffer;
        VkDeviceMemory memory;
};

#define FV_PERSON_PAINTER_INSTANCES_PER_BUFFER 16

static bool
create_texture(struct fv_person_painter *painter,
               const struct fv_image_data *image_data)
{
        VkResult res;

        res = fv_image_data_create_image_2d_array(image_data,
                                                  FV_N_ELEMENTS(textures),
                                                  textures,
                                                  &painter->texture_image,
                                                  &painter->texture_memory);
        if (res != VK_SUCCESS) {
                painter->texture_image = NULL;
                painter->texture_memory = NULL;
                fv_error_message("Error creating person texture");
                return false;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = painter->texture_image,
                .viewType =  VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                .format = fv_image_data_get_format(image_data, textures[0]),
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
                        fv_image_data_get_miplevels(image_data, textures[0]),
                        .baseArrayLayer = 0,
                        .layerCount = FV_N_ELEMENTS(textures)
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
create_sampler(struct fv_person_painter *painter)
{
        VkResult res;

        VkSamplerCreateInfo sampler_create_info = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1,
                .minLod = -1000.0f,
                .maxLod = 1000.0f
        };
        res = fv_vk.vkCreateSampler(painter->vk_data->device,
                                    &sampler_create_info,
                                    NULL, /* allocator */
                                    &painter->sampler);
        if (res != VK_SUCCESS) {
                painter->sampler = NULL;
                fv_error_message("Error creating sampler");
                return false;
        }

        return true;
}

static bool
create_descriptor_set(struct fv_person_painter *painter,
                      const struct fv_pipeline_data *pipeline_data)
{
        VkResult res;

        VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = painter->vk_data->descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = (pipeline_data->dsls +
                                FV_PIPELINE_DATA_DSL_TEXTURE)
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
                .sampler = painter->sampler,
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

struct fv_person_painter *
fv_person_painter_new(const struct fv_vk_data *vk_data,
                      const struct fv_pipeline_data *pipeline_data,
                      const struct fv_image_data *image_data)
{
        struct fv_person_painter *painter = fv_calloc(sizeof *painter);

        painter->vk_data = vk_data;
        painter->pipeline =
                pipeline_data->pipelines[FV_PIPELINE_DATA_PIPELINE_PERSON];
        painter->layout =
                pipeline_data->layouts[FV_PIPELINE_DATA_LAYOUT_TEXTURE];
        fv_list_init(&painter->instance_buffers);
        fv_list_init(&painter->in_use_instance_buffers);

        if (!fv_model_load(vk_data, &painter->model, "person.ply"))
                goto error;
        painter->model_loaded = true;

        if (!create_texture(painter, image_data))
                goto error;

        if (!create_sampler(painter))
                goto error;

        if (!create_descriptor_set(painter, pipeline_data))
                goto error;

        return painter;

error:
        fv_person_painter_free(painter);
        return NULL;
}

struct paint_closure {
        struct fv_person_painter *painter;
        const struct fv_paint_state *paint_state;
        struct fv_transform transform;

        struct fv_instance_person *instance_buffer_map;
        int n_instances;

        VkCommandBuffer command_buffer;
};

static void
flush_people(struct paint_closure *data)
{
        struct fv_person_painter *painter = data->painter;
        struct instance_buffer *instance_buffer;

        if (data->n_instances == 0)
                return;

        instance_buffer = fv_container_of(painter->in_use_instance_buffers.next,
                                          struct instance_buffer,
                                          link);

        fv_vk.vkUnmapMemory(painter->vk_data->device, instance_buffer->memory);
        data->instance_buffer_map = NULL;

        fv_vk.vkCmdBindPipeline(data->command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                painter->pipeline);
        fv_vk.vkCmdBindDescriptorSets(data->command_buffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      painter->layout,
                                      0, /* firstSet */
                                      1, /* descriptorSetCount */
                                      &painter->descriptor_set,
                                      0, /* dynamicOffsetCount */
                                      NULL /* pDynamicOffsets */);
        fv_vk.vkCmdBindVertexBuffers(data->command_buffer,
                                     0, /* firstBinding */
                                     2, /* bindingCount */
                                     (VkBuffer[]) {
                                             painter->model.buffer,
                                             instance_buffer->buffer,
                                     },
                                     (VkDeviceSize[]) {
                                             painter->model.vertices_offset,
                                             0
                                     });
        fv_vk.vkCmdBindIndexBuffer(data->command_buffer,
                                   painter->model.buffer,
                                   painter->model.indices_offset,
                                   VK_INDEX_TYPE_UINT16);
        fv_vk.vkCmdDrawIndexed(data->command_buffer,
                               painter->model.n_indices,
                               data->n_instances,
                               0, /* firstIndex */
                               0, /* vertexOffset */
                               0 /* firstInstance */);

        data->n_instances = 0;
}

static struct instance_buffer *
create_instance_buffer(struct fv_person_painter *painter)
{
        struct instance_buffer *instance_buffer;
        VkBuffer buffer;
        VkDeviceMemory memory;
        int buffer_offset;
        VkResult res;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = (sizeof (struct fv_instance_person) *
                         FV_PERSON_PAINTER_INSTANCES_PER_BUFFER),
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
                                       &buffer_offset);
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

        return instance_buffer;
}

static bool
start_instance(struct paint_closure *data)
{
        struct fv_person_painter *painter = data->painter;
        struct instance_buffer *buffer;
        VkResult res;

        if (data->n_instances >= FV_PERSON_PAINTER_INSTANCES_PER_BUFFER)
                flush_people(data);

        if (data->instance_buffer_map != NULL)
                return true;

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
                                (void **) &data->instance_buffer_map);
        if (res != VK_SUCCESS) {
                fv_error_message("Error mapping instance memory");
                data->instance_buffer_map = NULL;
                fv_list_insert(&painter->instance_buffers, &buffer->link);
                return false;
        }

        fv_list_insert(&painter->in_use_instance_buffers, &buffer->link);

        return true;
}

static void
paint_person_cb(const struct fv_logic_person *person,
                void *user_data)
{
        struct fv_instance_person *instance;
        struct paint_closure *data = user_data;
        float green_tint;

        /* Don't paint people that are out of the visible range */
        if (fabsf(person->x - data->paint_state->center_x) - 0.5f >=
            data->paint_state->visible_w / 2.0f ||
            fabsf(person->y - data->paint_state->center_y) - 0.5f >=
            data->paint_state->visible_h / 2.0f)
                return;

        if (!start_instance(data))
                return;

        data->transform.modelview = data->paint_state->transform.modelview;
        fv_matrix_translate(&data->transform.modelview,
                            person->x, person->y, 0.0f);
        fv_matrix_rotate(&data->transform.modelview,
                         person->direction * 180.f / M_PI,
                         0.0f, 0.0f, 1.0f);
        fv_transform_dirty(&data->transform);
        fv_transform_ensure_mvp(&data->transform);
        fv_transform_ensure_normal_transform(&data->transform);

        green_tint = person->esperantified ? 120 : 0;

        instance = data->instance_buffer_map + data->n_instances;
        memcpy(instance->mvp,
               &data->transform.mvp.xx,
               sizeof instance->mvp);
        memcpy(instance->normal_transform,
               data->transform.normal_transform,
               sizeof instance->normal_transform);
        instance->tex_layer = person->type;
        instance->green_tint = green_tint;

        data->n_instances++;
}

void
fv_person_painter_paint(struct fv_person_painter *painter,
                        struct fv_logic *logic,
                        VkCommandBuffer command_buffer,
                        const struct fv_paint_state *paint_state)
{
        struct paint_closure data;

        data.painter = painter;
        data.paint_state = paint_state;
        data.transform.projection = paint_state->transform.projection;
        data.n_instances = 0;
        data.instance_buffer_map = NULL;
        data.command_buffer = command_buffer;

        fv_list_insert_list(&painter->instance_buffers,
                            &painter->in_use_instance_buffers);
        fv_list_init(&painter->in_use_instance_buffers);

        fv_logic_for_each_person(logic, paint_person_cb, &data);

        flush_people(&data);
}

static void
free_instance_buffers(struct fv_person_painter *painter,
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
fv_person_painter_free(struct fv_person_painter *painter)
{
        free_instance_buffers(painter, &painter->instance_buffers);
        free_instance_buffers(painter, &painter->in_use_instance_buffers);

        if (painter->descriptor_set) {
                fv_vk.vkFreeDescriptorSets(painter->vk_data->device,
                                           painter->vk_data->descriptor_pool,
                                           1, /* descriptorSetCount */
                                           &painter->descriptor_set);
        }
        if (painter->sampler) {
                fv_vk.vkDestroySampler(painter->vk_data->device,
                                       painter->sampler,
                                       NULL /* allocator */);
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
        if (painter->model_loaded)
                fv_model_destroy(painter->vk_data, &painter->model);

        fv_free(painter);
}
