/*
 * Finvenkisto
 *
 * Copyright (C) 2016, 2017 Neil Roberts
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
        FV_PIPELINE_DATA_SHADER_HUD_VERTEX,
        FV_PIPELINE_DATA_SHADER_SPECIAL_COLOR_VERTEX,
        FV_PIPELINE_DATA_SHADER_SPECIAL_TEXTURE_VERTEX,
        FV_PIPELINE_DATA_SHADER_PERSON_VERTEX,
        FV_PIPELINE_DATA_SHADER_TEXTURE_VERTEX,
        FV_PIPELINE_DATA_SHADER_HIGHLIGHT_VERTEX,
        FV_PIPELINE_DATA_SHADER_COLOR_FRAGMENT,
        FV_PIPELINE_DATA_SHADER_TEXTURE_FRAGMENT,
        FV_PIPELINE_DATA_SHADER_LIGHTING_TEXTURE_FRAGMENT,
        FV_PIPELINE_DATA_SHADER_PERSON_FRAGMENT,
};

struct fv_pipeline_data_shader_data {
        const char *filename;
};

static const struct fv_pipeline_data_shader_data
shader_data[] = {
        [FV_PIPELINE_DATA_SHADER_MAP_VERTEX] = {
                .filename = "fv-map-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_HUD_VERTEX] = {
                .filename = "fv-hud-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_SPECIAL_COLOR_VERTEX] = {
                .filename = "fv-special-color-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_SPECIAL_TEXTURE_VERTEX] = {
                .filename = "fv-special-texture-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_PERSON_VERTEX] = {
                .filename = "fv-person-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_TEXTURE_VERTEX] = {
                .filename = "fv-texture-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_HIGHLIGHT_VERTEX] = {
                .filename = "fv-highlight-vertex.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_COLOR_FRAGMENT] = {
                .filename = "fv-color-fragment.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_TEXTURE_FRAGMENT] = {
                .filename = "fv-texture-fragment.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_LIGHTING_TEXTURE_FRAGMENT] = {
                .filename = "fv-lighting-texture-fragment.spirv"
        },
        [FV_PIPELINE_DATA_SHADER_PERSON_FRAGMENT] = {
                .filename = "fv-person-fragment.spirv"
        },
};

static const VkPipelineViewportStateCreateInfo
base_viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
        /* actual viewport and scissor state is dynamic */
};

static const VkPipelineRasterizationStateCreateInfo
base_rasterization_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
};

static const VkPipelineMultisampleStateCreateInfo
base_multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
};

static const VkPipelineDepthStencilStateCreateInfo
base_depth_stencil_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = true,
        .depthWriteEnable = true,
        .depthCompareOp = VK_COMPARE_OP_LESS
};

static const VkPipelineColorBlendAttachmentState base_blend_attachments[] = {
        {
                .blendEnable = false,
                .colorWriteMask = (VK_COLOR_COMPONENT_R_BIT |
                                   VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT)
        }
};

static const VkPipelineColorBlendStateCreateInfo base_color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = FV_N_ELEMENTS(base_blend_attachments),
        .pAttachments = base_blend_attachments
};

static const VkDynamicState base_dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
};

static const VkPipelineDynamicStateCreateInfo base_dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = FV_N_ELEMENTS(base_dynamic_states),
        .pDynamicStates = base_dynamic_states
};

static const VkGraphicsPipelineCreateInfo
base_pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pViewportState = &base_viewport_state,
        .pRasterizationState = &base_rasterization_state,
        .pMultisampleState = &base_multisample_state,
        .pDepthStencilState = &base_depth_stencil_state,
        .pColorBlendState = &base_color_blend_state,
        .pDynamicState = &base_dynamic_state,
        .subpass = 0,
        .basePipelineHandle = NULL,
        .basePipelineIndex = -1
};

static const VkPipelineColorBlendAttachmentState
base_blend_enabled_attachments[] = {
        {
                .blendEnable = true,
                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = (VK_COLOR_COMPONENT_R_BIT |
                                   VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT)
        }
};

static const VkPipelineColorBlendStateCreateInfo base_blend_enabled_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = FV_N_ELEMENTS(base_blend_enabled_attachments),
        .pAttachments = base_blend_enabled_attachments
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
create_mipmap_sampler(const struct fv_vk_data *vk_data,
                      struct fv_pipeline_data *data)
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
        VkSampler *sampler = data->samplers + FV_PIPELINE_DATA_SAMPLER_MIPMAP;
        res = fv_vk.vkCreateSampler(vk_data->device,
                                    &sampler_create_info,
                                    NULL, /* allocator */
                                    sampler);
        if (res != VK_SUCCESS) {
                sampler = NULL;
                fv_error_message("Error creating mipmap sampler");
                return false;
        }

        return true;
}

static bool
create_nearest_sampler(const struct fv_vk_data *vk_data,
                       struct fv_pipeline_data *data)
{
        VkResult res;

        VkSamplerCreateInfo sampler_create_info = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_NEAREST,
                .minFilter = VK_FILTER_NEAREST,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1
        };
        VkSampler *sampler = data->samplers + FV_PIPELINE_DATA_SAMPLER_NEAREST;
        res = fv_vk.vkCreateSampler(vk_data->device,
                                    &sampler_create_info,
                                    NULL, /* allocator */
                                    sampler);
        if (res != VK_SUCCESS) {
                sampler = NULL;
                fv_error_message("Error creating nearest sampler");
                return false;
        }

        return true;
}

static bool
create_texture_dsl(const struct fv_vk_data *vk_data,
                   struct fv_pipeline_data *data,
                   enum fv_pipeline_data_sampler sampler_num,
                   enum fv_pipeline_data_dsl dsl_num)
{
        VkResult res;

        VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[] = {
                {
                        .binding = 0,
                        .descriptorType =
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .pImmutableSamplers = data->samplers + sampler_num
                }
        };

        VkDescriptorSetLayoutCreateInfo dsl_create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = FV_N_ELEMENTS(descriptor_set_layout_bindings),
                .pBindings = descriptor_set_layout_bindings
        };
        res = fv_vk.vkCreateDescriptorSetLayout(vk_data->device,
                                                &dsl_create_info,
                                                NULL, /* allocator */
                                                data->dsls + dsl_num);
        if (res != VK_SUCCESS) {
                data->dsls[dsl_num] = NULL;
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
                .pSetLayouts = data->dsls + FV_PIPELINE_DATA_DSL_TEXTURE_MIPMAP
        };
        VkPipelineLayout *layout =
                data->layouts + FV_PIPELINE_DATA_LAYOUT_MAP;
        res = fv_vk.vkCreatePipelineLayout(vk_data->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           layout);
        if (res != VK_SUCCESS) {
                *layout = NULL;
                fv_error_message("Error creating map pipeline layout");
                return false;
        }

        return true;
}

static bool
create_texture_layout(const struct fv_vk_data *vk_data,
                      struct fv_pipeline_data *data,
                      enum fv_pipeline_data_dsl dsl_num,
                      enum fv_pipeline_data_layout layout_num)
{
        VkResult res;

        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = data->dsls + dsl_num
        };
        res = fv_vk.vkCreatePipelineLayout(vk_data->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           data->layouts + layout_num);
        if (res != VK_SUCCESS) {
                data->layouts[layout_num] = NULL;
                fv_error_message("Error creating texture pipeline layout");
                return false;
        }

        return true;
}

static bool
create_empty_layout(const struct fv_vk_data *vk_data,
                    struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 0,
                .setLayoutCount = 0
        };
        VkPipelineLayout *layout =
                data->layouts + FV_PIPELINE_DATA_LAYOUT_EMPTY;
        res = fv_vk.vkCreatePipelineLayout(vk_data->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           layout);
        if (res != VK_SUCCESS) {
                *layout = NULL;
                fv_error_message("Error creating empty layout");
                return false;
        }

        return true;
}

static bool
create_special_texture_layout(const struct fv_vk_data *vk_data,
                              struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 0,
                .setLayoutCount = 1,
                .pSetLayouts = data->dsls + FV_PIPELINE_DATA_DSL_TEXTURE_MIPMAP
        };
        VkPipelineLayout *layout =
                data->layouts + FV_PIPELINE_DATA_LAYOUT_SPECIAL_TEXTURE;
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
create_shout_layout(const struct fv_vk_data *vk_data,
                    struct fv_pipeline_data *data)
{
        VkResult res;

        VkPushConstantRange push_constant_ranges[] = {
                {
                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                        .offset = 0,
                        .size = sizeof (struct fv_vertex_shout_push_constants)
                }
        };

        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = FV_N_ELEMENTS(push_constant_ranges),
                .pPushConstantRanges = push_constant_ranges,
                .setLayoutCount = 1,
                .pSetLayouts = data->dsls + FV_PIPELINE_DATA_DSL_TEXTURE_MIPMAP
        };
        VkPipelineLayout *layout =
                data->layouts + FV_PIPELINE_DATA_LAYOUT_SHOUT;
        res = fv_vk.vkCreatePipelineLayout(vk_data->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           layout);
        if (res != VK_SUCCESS) {
                *layout = NULL;
                fv_error_message("Error creating shout pipeline layout");
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
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module = shaders[FV_PIPELINE_DATA_SHADER_MAP_VERTEX],
                        .pName = "main"
                },
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module = shaders
                        [FV_PIPELINE_DATA_SHADER_LIGHTING_TEXTURE_FRAGMENT],
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

        VkGraphicsPipelineCreateInfo info = base_pipeline_create_info;

        info.stageCount = FV_N_ELEMENTS(stages);
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_MAP];
        info.renderPass = render_pass;

        VkPipeline *pipeline =
                data->pipelines + FV_PIPELINE_DATA_PIPELINE_MAP;
        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              pipeline);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating map pipeline");
                return false;
        }

        return true;
}

static bool
create_hud_pipeline(const struct fv_vk_data *vk_data,
                    VkRenderPass render_pass,
                    VkPipelineCache pipeline_cache,
                    VkShaderModule *shaders,
                    struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineShaderStageCreateInfo stages[] = {
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module = shaders[FV_PIPELINE_DATA_SHADER_HUD_VERTEX],
                        .pName = "main"
                },
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module = shaders
                        [FV_PIPELINE_DATA_SHADER_TEXTURE_FRAGMENT],
                        .pName = "main"
                },
        };
        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct fv_vertex_hud),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                }
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_hud, x)
                },
                {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_hud, s)
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

        VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = false,
                .depthWriteEnable = false
        };

        VkGraphicsPipelineCreateInfo info = base_pipeline_create_info;

        info.stageCount = FV_N_ELEMENTS(stages);
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_TEXTURE_NEAREST];
        info.renderPass = render_pass;
        info.pColorBlendState = &base_blend_enabled_state;
        info.pDepthStencilState = &depth_stencil_state;

        VkPipeline *pipeline =
                data->pipelines + FV_PIPELINE_DATA_PIPELINE_HUD;
        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              pipeline);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating map pipeline");
                return false;
        }

        return true;
}

static bool
create_person_pipeline(const struct fv_vk_data *vk_data,
                       VkRenderPass render_pass,
                       VkPipelineCache pipeline_cache,
                       VkShaderModule *shaders,
                       struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineShaderStageCreateInfo stages[] = {
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_PERSON_VERTEX],
                        .pName = "main"
                },
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_PERSON_FRAGMENT],
                        .pName = "main"
                },
        };
        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct fv_vertex_model_texture),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                },
                {
                        .binding = 1,
                        .stride = sizeof (struct fv_instance_person),
                        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
                }
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_texture, x)
                },
                {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_texture, s)
                },
                {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_texture, nx)
                },
                {
                        .location = 3,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_person,
                                           mvp[0])
                },
                {
                        .location = 4,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_person,
                                           mvp[4])
                },
                {
                        .location = 5,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_person,
                                           mvp[8])
                },
                {
                        .location = 6,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_person,
                                           mvp[12])
                },
                {
                        .location = 7,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_person,
                                           normal_transform[0])
                },
                {
                        .location = 8,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_person,
                                           normal_transform[3])
                },
                {
                        .location = 9,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_person,
                                           normal_transform[6])
                },
                {
                        .location = 10,
                        .binding = 1,
                        .format = VK_FORMAT_R8_USCALED,
                        .offset = offsetof(struct fv_instance_person,
                                           tex_layer)
                },
                {
                        .location = 11,
                        .binding = 1,
                        .format = VK_FORMAT_R8_UNORM,
                        .offset = offsetof(struct fv_instance_person,
                                           green_tint)
                },
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

        VkGraphicsPipelineCreateInfo info = base_pipeline_create_info;

        info.stageCount = FV_N_ELEMENTS(stages);
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_TEXTURE_MIPMAP];
        info.renderPass = render_pass;

        VkPipeline *pipeline =
                data->pipelines + FV_PIPELINE_DATA_PIPELINE_PERSON;
        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              pipeline);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating person pipeline");
                return false;
        }

        return true;
}

static bool
create_special_color_pipeline(const struct fv_vk_data *vk_data,
                              VkRenderPass render_pass,
                              VkPipelineCache pipeline_cache,
                              VkShaderModule *shaders,
                              struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineShaderStageCreateInfo stages[] = {
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_SPECIAL_COLOR_VERTEX],
                        .pName = "main"
                },
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_COLOR_FRAGMENT],
                        .pName = "main"
                },
        };
        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct fv_vertex_model_color),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                },
                {
                        .binding = 1,
                        .stride = sizeof (struct fv_instance_special),
                        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
                },
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_color, x)
                },
                {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_color, nx)
                },
                {
                        .location = 3,
                        .binding = 0,
                        .format = VK_FORMAT_R8G8B8_UNORM,
                        .offset = offsetof(struct fv_vertex_model_color, r)
                },
                {
                        .location = 4,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[0])
                },
                {
                        .location = 5,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[4])
                },
                {
                        .location = 6,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[8])
                },
                {
                        .location = 7,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[12])
                },
                {
                        .location = 8,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           normal_transform[0])
                },
                {
                        .location = 9,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           normal_transform[3])
                },
                {
                        .location = 10,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           normal_transform[6])
                },
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

        VkGraphicsPipelineCreateInfo info = base_pipeline_create_info;

        info.stageCount = FV_N_ELEMENTS(stages);
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_EMPTY];
        info.renderPass = render_pass;

        VkPipeline *pipeline =
                data->pipelines + FV_PIPELINE_DATA_PIPELINE_SPECIAL_COLOR;
        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              pipeline);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating special color pipeline");
                return false;
        }

        return true;
}

static bool
create_special_texture_pipeline(const struct fv_vk_data *vk_data,
                                VkRenderPass render_pass,
                                VkPipelineCache pipeline_cache,
                                VkShaderModule *shaders,
                                struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineShaderStageCreateInfo stages[] = {
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_SPECIAL_TEXTURE_VERTEX],
                        .pName = "main"
                },
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module = shaders
                        [FV_PIPELINE_DATA_SHADER_LIGHTING_TEXTURE_FRAGMENT],
                        .pName = "main"
                },
        };
        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct fv_vertex_model_texture),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                },
                {
                        .binding = 1,
                        .stride = sizeof (struct fv_instance_special),
                        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
                },
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_texture, x)
                },
                {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_texture, s)
                },
                {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_model_texture, nx)
                },
                {
                        .location = 4,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[0])
                },
                {
                        .location = 5,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[4])
                },
                {
                        .location = 6,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[8])
                },
                {
                        .location = 7,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           modelview[12])
                },
                {
                        .location = 8,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           normal_transform[0])
                },
                {
                        .location = 9,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           normal_transform[3])
                },
                {
                        .location = 10,
                        .binding = 1,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_instance_special,
                                           normal_transform[6])
                },
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

        VkGraphicsPipelineCreateInfo info = base_pipeline_create_info;

        info.stageCount = FV_N_ELEMENTS(stages);
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_SPECIAL_TEXTURE];
        info.renderPass = render_pass;

        VkPipeline *pipeline =
                data->pipelines + FV_PIPELINE_DATA_PIPELINE_SPECIAL_TEXTURE;
        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              pipeline);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating special texture pipeline");
                return false;
        }

        return true;
}

static bool
create_shout_pipeline(const struct fv_vk_data *vk_data,
                      VkRenderPass render_pass,
                      VkPipelineCache pipeline_cache,
                      VkShaderModule *shaders,
                      struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineShaderStageCreateInfo stages[] = {
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_TEXTURE_VERTEX],
                        .pName = "main"
                },
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_TEXTURE_FRAGMENT],
                        .pName = "main"
                },
        };
        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct fv_vertex_shout),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                }
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_shout, x)
                },
                {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_shout, s)
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

        VkGraphicsPipelineCreateInfo info = base_pipeline_create_info;

        info.stageCount = FV_N_ELEMENTS(stages);
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_MAP];
        info.renderPass = render_pass;
        info.pColorBlendState = &base_blend_enabled_state;

        VkPipeline *pipeline =
                data->pipelines + FV_PIPELINE_DATA_PIPELINE_SHOUT;
        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              pipeline);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating shout pipeline");
                return false;
        }

        return true;
}

static bool
create_highlight_pipeline(const struct fv_vk_data *vk_data,
                          VkRenderPass render_pass,
                          VkPipelineCache pipeline_cache,
                          VkShaderModule *shaders,
                          struct fv_pipeline_data *data)
{
        VkResult res;

        VkPipelineShaderStageCreateInfo stages[] = {
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_HIGHLIGHT_VERTEX],
                        .pName = "main"
                },
                {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module =
                        shaders[FV_PIPELINE_DATA_SHADER_COLOR_FRAGMENT],
                        .pName = "main"
                },
        };
        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct fv_vertex_highlight),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                }
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct fv_vertex_highlight, x)
                },
                {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R8G8B8A8_UNORM,
                        .offset = offsetof(struct fv_vertex_highlight, r)
                },
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

        VkGraphicsPipelineCreateInfo info = base_pipeline_create_info;

        info.stageCount = FV_N_ELEMENTS(stages);
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.layout = data->layouts[FV_PIPELINE_DATA_LAYOUT_SHOUT];
        info.renderPass = render_pass;
        info.pColorBlendState = &base_blend_enabled_state;

        VkPipeline *pipeline =
                data->pipelines + FV_PIPELINE_DATA_PIPELINE_HIGHLIGHT;
        res = fv_vk.vkCreateGraphicsPipelines(vk_data->device,
                                              pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              pipeline);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating highlight pipeline");
                return false;
        }

        return true;
}

static bool
create_objects(const struct fv_vk_data *vk_data,
               VkRenderPass render_pass,
               VkPipelineCache pipeline_cache,
               VkShaderModule *shaders,
               struct fv_pipeline_data *data)
{
        if (!create_mipmap_sampler(vk_data, data))
                return false;

        if (!create_nearest_sampler(vk_data, data))
                return false;

        if (!create_texture_dsl(vk_data,
                                data,
                                FV_PIPELINE_DATA_SAMPLER_MIPMAP,
                                FV_PIPELINE_DATA_DSL_TEXTURE_MIPMAP))
                return false;

        if (!create_texture_dsl(vk_data,
                                data,
                                FV_PIPELINE_DATA_SAMPLER_NEAREST,
                                FV_PIPELINE_DATA_DSL_TEXTURE_NEAREST))
                return false;

        if (!create_map_layout(vk_data, data))
                return false;

        if (!create_texture_layout(vk_data,
                                   data,
                                   FV_PIPELINE_DATA_DSL_TEXTURE_NEAREST,
                                   FV_PIPELINE_DATA_LAYOUT_TEXTURE_NEAREST))
                return false;

        if (!create_texture_layout(vk_data,
                                   data,
                                   FV_PIPELINE_DATA_DSL_TEXTURE_MIPMAP,
                                   FV_PIPELINE_DATA_LAYOUT_TEXTURE_MIPMAP))
                return false;

        if (!create_empty_layout(vk_data, data))
                return false;

        if (!create_special_texture_layout(vk_data, data))
                return false;

        if (!create_shout_layout(vk_data, data))
                return false;

        if (!create_map_pipeline(vk_data,
                                 render_pass,
                                 pipeline_cache,
                                 shaders,
                                 data))
                return false;

        if (!create_hud_pipeline(vk_data,
                                 render_pass,
                                 pipeline_cache,
                                 shaders,
                                 data))
                return false;

        if (!create_person_pipeline(vk_data,
                                    render_pass,
                                    pipeline_cache,
                                    shaders,
                                    data))
                return false;

        if (!create_special_color_pipeline(vk_data,
                                           render_pass,
                                           pipeline_cache,
                                           shaders,
                                           data))
                return false;

        if (!create_special_texture_pipeline(vk_data,
                                             render_pass,
                                             pipeline_cache,
                                             shaders,
                                             data))
                return false;

        if (!create_shout_pipeline(vk_data,
                                   render_pass,
                                   pipeline_cache,
                                   shaders,
                                   data))
                return false;

        if (!create_highlight_pipeline(vk_data,
                                       render_pass,
                                       pipeline_cache,
                                       shaders,
                                       data))
                return false;

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

                if (!create_objects(vk_data,
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
        for (i = 0; i < FV_PIPELINE_DATA_N_SAMPLERS; i++) {
                if (data->samplers[i] == NULL)
                        continue;
                fv_vk.vkDestroySampler(vk_data->device,
                                       data->samplers[i],
                                       NULL /* allocator */);
        }
}
