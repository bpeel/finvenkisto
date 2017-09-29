/*
 * Finvenkisto
 *
 * Copyright (C) 2016 Neil Roberts
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "fv-pipeline-data.h"
#include "fv-util.h"
#include "fv-data.h"
#include "fv-vk.h"
#include "fv-error-message.h"
#include "fv-vertex.h"

enum fv_pipeline_data_shader {
        FV_PIPELINE_DATA_SHADER_MAP_VERTEX,
        FV_PIPELINE_DATA_SHADER_TEXTURE_FRAGMENT,
};

struct fv_pipeline_data_shader_data {
        const char *filename;
};

static const struct fv_pipeline_data_shader_data
shader_data[] = {
        [FV_PIPELINE_DATA_SHADER_MAP_VERTEX] = {
                .filename = "fv-map-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_TEXTURE_FRAGMENT] = {
                .filename = "fv-lighting-texture-fragment.spirv"
        },
};

static bool
load_shaders(VkDevice device,
             VkShaderModule shaders[FV_N_ELEMENTS(shader_data)])
{
        char *shader_filename;
        char *full_filename;
        uint8_t *buf;
        long int file_size;
        size_t got;
        FILE *file;
        VkResult res;
        int i;

        for (i = 0; i < FV_N_ELEMENTS(shader_data); i++) {
                shader_filename = fv_strconcat("shaders"
                                               FV_PATH_SEPARATOR,
                                               shader_data[i].filename,
                                               NULL);
                full_filename = fv_data_get_filename(shader_filename);
                fv_free(shader_filename);
                if (full_filename == NULL) {
                        fv_error_message("Error getting filename for %s",
                                         shader_data[i].filename);
                        goto error;
                }

                file = fopen(full_filename, "rb");
                fv_free(full_filename);

                if (file == NULL) {
                        fv_error_message("%s: %s",
                                         shader_data[i].filename,
                                         strerror(errno));
                        goto error;
                }

                if (fseek(file, 0, SEEK_END))
                        goto error_file;

                file_size = ftell(file);
                if (file_size == -1)
                        goto error_file;

                if (fseek(file, 0, SEEK_SET))
                        goto error_file;

                buf = fv_alloc(file_size);
                got = fread(buf, 1, file_size, file);
                if (got != file_size) {
                        fv_free(buf);
                        goto error_file;
                }

                fclose(file);

                VkShaderModuleCreateInfo shader_module_create_info = {
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = file_size,
                        .pCode = (const uint32_t *) buf
                };
                res = fv_vk.vkCreateShaderModule(device,
                                                 &shader_module_create_info,
                                                 NULL, /* allocator */
                                                 shaders + i);

                fv_free(buf);

                if (res != VK_SUCCESS) {
                        fv_error_message("Failed to create shader for %s",
                                         shader_data[i].filename);
                        goto error;
                }
        }

        return true;

error_file:
        fv_error_message("%s: %s",
                         shader_data[i].filename,
                         strerror(errno));
        fclose(file);

error:
        while (--i >= 0) {
                fv_vk.vkDestroyShaderModule(device,
                                            shaders[i],
                                            NULL /* allocator */);
        }

        return false;
}

static bool
create_texture_dsl(const struct fv_vk_data *vk_data,
                   struct fv_pipeline_data *data)
{
        VkResult res;

        VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[] = {
                {
                        .binding = 0,
                        .descriptorType =
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                }
        };

        VkDescriptorSetLayoutCreateInfo dsl_create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = FV_N_ELEMENTS(descriptor_set_layout_bindings),
                .pBindings = descriptor_set_layout_bindings
        };
        VkDescriptorSetLayout *dsl = data->dsls + FV_PIPELINE_DATA_DSL_TEXTURE;
        res = fv_vk.vkCreateDescriptorSetLayout(vk_data->device,
                                                &dsl_create_info,
                                                NULL, /* allocator */
                                                dsl);
        if (res != VK_SUCCESS) {
                *dsl = NULL;
                fv_error_message("Error creating descriptor set layout");
                return false;
        }

        return true;
}

static bool
create_map_layout(const struct fv_vk_data *vk_data,
                  struct fv_pipeline_data *data)
{
        VkResult res;

        VkPushConstantRange push_constant_ranges[] = {
                {
                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                        .offset = 0,
                        .size = sizeof (struct fv_vertex_map_push_constants)
                }
        };

        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = FV_N_ELEMENTS(push_constant_ranges),
                .pPushConstantRanges = push_constant_ranges,
                .setLayoutCount = 1,
                .pSetLayouts = data->dsls + FV_PIPELINE_DATA_DSL_TEXTURE
        };
        VkPipelineLayout *layout =
                data->layouts + FV_PIPELINE_DATA_LAYOUT_MAP;
        res = fv_vk.vkCreatePipelineLayout(vk_data->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           layout);
        if (res != VK_SUCCESS) {
                *layout = NULL;
                fv_error_message("Error creating pipeline layout");
                return false;
        }

        return true;
}

static bool
create_map_pipeline(const struct fv_vk_data *vk_data,
                    VkRenderPass render_pass,
                    VkPipelineCache pipeline_cache,
                    VkShaderModule *shaders,
                    struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineShaderStageCreateInfo stages[] = {
                {
                        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module = shaders[FV_PIPELINE_DATA_SHADER_MAP_VERTEX],
                        .pName = "main"
                },
                {
                        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_TEXTURE_FRAGMENT],
                        .pName = "main"
                },
        };
        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct fv_vertex_map),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                }
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R8G8B8A8_USCALED,
                        .offset = offsetof(struct fv_vertex_map, x)
                },
                {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R16G16_UNORM,
                        .offset = offsetof(struct fv_vertex_map, s)
                }
        };
        VkPipelineVertexInputStateCreateInfo vertex_input_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount =
                FV_N_ELEMENTS(input_binding_descriptions),
                .pVertexBindingDescriptions = input_binding_descriptions,
                .vertexAttributeDescriptionCount =
                FV_N_ELEMENTS(attribute_descriptions),
                .pVertexAttributeDescriptions = attribute_descriptions
        };
        VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .primitiveRestartEnable = false
        };
        VkPipelineViewportStateCreateInfo viewport_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1
                /* actual viewport and scissor state is dynamic */
        };
        VkPipelineRasterizationStateCreateInfo rasterization_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f
        };
        VkPipelineMultisampleStateCreateInfo multisample_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        };
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = VK_COMPARE_OP_LESS
        };
        VkPipelineColorBlendAttachmentState blend_attachments[] = {
                {
                        .blendEnable = false,
                        .colorWriteMask = (VK_COLOR_COMPONENT_R_BIT |
                                           VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT |
                                           VK_COLOR_COMPONENT_A_BIT)
                }
        };
        VkPipelineColorBlendStateCreateInfo color_blend_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = FV_N_ELEMENTS(blend_attachments),
                .pAttachments = blend_attachments
        };
        VkDynamicState dynamic_states[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamic_state ={
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = FV_N_ELEMENTS(dynamic_states),
                .pDynamicStates = dynamic_states
        };
        VkGraphicsPipelineCreateInfo pipeline_create_infos[] = {
                {
                        .sType =
                        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                        .stageCount = FV_N_ELEMENTS(stages),
                        .pStages = stages,
                        .pVertexInputState = &vertex_input_state,
                        .pInputAssemblyState = &input_assembly_state,
                        .pViewportState = &viewport_state,
                        .pRasterizationState = &rasterization_state,
                        .pMultisampleState = &multisample_state,
                        .pDepthStencilState = &depth_stencil_state,
                        .pColorBlendState = &color_blend_state,
                        .pDynamicState = &dynamic_state,
                        .layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_MAP],
                        .renderPass = render_pass,
                        .subpass = 0,
                        .basePipelineHandle = NULL,
                        .basePipelineIndex = -1,
                }
        };
        const int n_infos = FV_N_ELEMENTS(pipeline_create_infos);

        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              n_infos,
                                              pipeline_create_infos,
                                              NULL, /* allocator */
                                              data->pipelines);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating pipeline");
                return false;
        }

        return true;
}

bool
fv_pipeline_data_init(const struct fv_vk_data *vk_data,
                      VkRenderPass render_pass,
                      struct fv_pipeline_data *data)
{
        VkShaderModule shaders[FV_N_ELEMENTS(shader_data)];
        VkPipelineCache pipeline_cache;
        bool ret = true;
        VkResult res;
        int i;

        if (!load_shaders(vk_data->device, shaders))
                return false;

        VkPipelineCacheCreateInfo pipeline_cache_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
        };
        res = fv_vk.vkCreatePipelineCache(vk_data->device,
                                          &pipeline_cache_create_info,
                                          NULL, /* allocator */
                                          &pipeline_cache);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating pipeline cache");
                ret = false;
        } else {
                memset(data, 0, sizeof *data);

                if (!create_texture_dsl(vk_data, data) ||
                    !create_map_layout(vk_data, data) ||
                    !create_map_pipeline(vk_data,
                                         render_pass,
                                         pipeline_cache,
                                         shaders,
                                         data)) {
                        fv_pipeline_data_destroy(vk_data, data);
                        ret = false;
                }

                fv_vk.vkDestroyPipelineCache(vk_data->device,
                                             pipeline_cache,
                                             NULL /* allocator */);
        }

        for (i = 0; i < FV_N_ELEMENTS(shader_data); i++) {
                fv_vk.vkDestroyShaderModule(vk_data->device,
                                            shaders[i],
                                            NULL /* allocator */);
        }

        return ret;
}

void
fv_pipeline_data_destroy(const struct fv_vk_data *vk_data,
                         struct fv_pipeline_data *data)
{
        int i;

        for (i = 0; i < FV_PIPELINE_DATA_N_DSLS; i++) {
                if (data->dsls[i] == NULL)
                        continue;
                fv_vk.vkDestroyDescriptorSetLayout(vk_data->device,
                                                   data->dsls[i],
                                                   NULL /* allocator */);
        }
        for (i = 0; i < FV_PIPELINE_DATA_N_LAYOUTS; i++) {
                if (data->layouts[i] == NULL)
                        continue;
                fv_vk.vkDestroyPipelineLayout(vk_data->device,
                                              data->layouts[i],
                                              NULL /* allocator */);
        }
        for (i = 0; i < FV_PIPELINE_DATA_N_PIPELINES; i++) {
                if (data->pipelines[i] == NULL)
                        continue;
                fv_vk.vkDestroyPipeline(vk_data->device,
                                        data->pipelines[i],
                                        NULL /* allocator */);
        }
}
