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

#include <assert.h>
#include <stdarg.h>

#include "fv-hud.h"
#include "fv-util.h"
#include "fv-ease.h"
#include "fv-vertex.h"
#include "fv-error-message.h"
#include "fv-allocate-store.h"

struct fv_hud {
        const struct fv_vk_data *vk_data;

        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkBuffer buffer;
        VkDeviceMemory memory;
        void *memory_map;
        VkImage texture_image;
        VkDeviceMemory texture_memory;
        VkImageView texture_view;
        VkSampler sampler;
        VkDescriptorSet descriptor_set;

        int tex_width, tex_height;

        int n_rectangles;
        struct fv_vertex_hud *vertex;
        int screen_width, screen_height;
};

struct fv_hud_image {
        int x, y, w, h;
};

enum fv_hud_alignment {
        FV_HUD_ALIGNMENT_LEFT,
        FV_HUD_ALIGNMENT_CENTER,
        FV_HUD_ALIGNMENT_RIGHT
};

#include "data/hud-layout.h"

static const struct fv_hud_image *
fv_hud_key_images[] = {
        &fv_hud_image_up,
        &fv_hud_image_down,
        &fv_hud_image_left,
        &fv_hud_image_right,
        &fv_hud_image_shout
};

static const struct fv_hud_image *
fv_hud_digit_images[] = {
        &fv_hud_image_digit0,
        &fv_hud_image_digit1,
        &fv_hud_image_digit2,
        &fv_hud_image_digit3,
        &fv_hud_image_digit4,
        &fv_hud_image_digit5,
        &fv_hud_image_digit6,
        &fv_hud_image_digit7,
        &fv_hud_image_digit8,
        &fv_hud_image_digit9,
};

#define FV_HUD_MAX_RECTANGLES 16
#define FV_HUD_INDICES_OFFSET (sizeof (struct fv_vertex_hud) * 4 * \
                               FV_HUD_MAX_RECTANGLES)

#define FV_HUD_FINA_VENKO_SLIDE_TIME 1.0f

static bool
create_texture(struct fv_hud *hud,
               const struct fv_image_data *image_data)
{
        VkResult res;

        fv_image_data_get_size(image_data,
                               FV_IMAGE_DATA_HUD,
                               &hud->tex_width,
                               &hud->tex_height);
        res = fv_image_data_create_image_2d(image_data,
                                            FV_IMAGE_DATA_HUD,
                                            &hud->texture_image,
                                            &hud->texture_memory);
        if (res != VK_SUCCESS) {
                hud->texture_image = NULL;
                hud->texture_memory = NULL;
                fv_error_message("Error creating hud texture");
                return false;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = hud->texture_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = fv_image_data_get_format(image_data,
                                                   FV_IMAGE_DATA_HUD),
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
                                                    FV_IMAGE_DATA_HUD),
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = fv_vk.vkCreateImageView(hud->vk_data->device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &hud->texture_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating hud image view");
                hud->texture_view = NULL;
                return false;
        }

        return true;
}

static bool
create_sampler(struct fv_hud *hud)
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
        res = fv_vk.vkCreateSampler(hud->vk_data->device,
                                    &sampler_create_info,
                                    NULL, /* allocator */
                                    &hud->sampler);
        if (res != VK_SUCCESS) {
                hud->sampler = NULL;
                fv_error_message("Error creating sampler");
                return false;
        }

        return true;
}

static bool
create_descriptor_set(struct fv_hud *hud,
                      const struct fv_pipeline_data *pipeline_data)
{
        VkResult res;

        VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = hud->vk_data->descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = (pipeline_data->dsls +
                                FV_PIPELINE_DATA_DSL_TEXTURE)
        };
        res = fv_vk.vkAllocateDescriptorSets(hud->vk_data->device,
                                             &descriptor_set_allocate_info,
                                             &hud->descriptor_set);
        if (res != VK_SUCCESS) {
                hud->descriptor_set = NULL;
                fv_error_message("Error allocating descriptor set");
                return false;
        }

        VkDescriptorImageInfo descriptor_image_info = {
                .sampler = hud->sampler,
                .imageView = hud->texture_view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkWriteDescriptorSet write_descriptor_set = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = hud->descriptor_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &descriptor_image_info
        };
        fv_vk.vkUpdateDescriptorSets(hud->vk_data->device,
                                     1, /* descriptorWriteCount */
                                     &write_descriptor_set,
                                     0, /* descriptorCopyCount */
                                     NULL /* pDescriptorCopies */);

        return true;
}

static bool
create_buffer(struct fv_hud *hud)
{
        VkResult res;
        int buffer_offset;

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = (FV_HUD_INDICES_OFFSET +
                         sizeof (uint16_t) *
                         FV_HUD_MAX_RECTANGLES * 6),
                .usage = (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = fv_vk.vkCreateBuffer(hud->vk_data->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &hud->buffer);
        if (res != VK_SUCCESS) {
                hud->buffer = NULL;
                fv_error_message("Error creating hud buffer");
                return false;
        }

        res = fv_allocate_store_buffer(hud->vk_data,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &hud->buffer,
                                       &hud->memory,
                                       &buffer_offset);
        if (res != VK_SUCCESS) {
                hud->memory = NULL;
                fv_error_message("Error creating hud memory");
                return false;
        }

        res = fv_vk.vkMapMemory(hud->vk_data->device,
                                hud->memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                &hud->memory_map);
        if (res != VK_SUCCESS) {
                hud->memory_map = NULL;
                fv_error_message("Error mapping hud memory");
                return false;
        }

        return true;
}

struct fv_hud *
fv_hud_new(const struct fv_vk_data *vk_data,
           const struct fv_pipeline_data *pipeline_data,
           const struct fv_image_data *image_data)
{
        struct fv_hud *hud;
        uint16_t *indices;
        int i;

        hud = fv_alloc(sizeof *hud);
        memset(hud, 0, sizeof *hud);

        hud->vk_data = vk_data;
        hud->pipeline = pipeline_data->pipelines[FV_PIPELINE_DATA_PIPELINE_HUD];
        hud->layout = pipeline_data->layouts[FV_PIPELINE_DATA_LAYOUT_HUD];

        if (!create_texture(hud, image_data))
                goto error;

        if (!create_sampler(hud))
                goto error;

        if (!create_descriptor_set(hud, pipeline_data))
                goto error;

        if (!create_buffer(hud))
                goto error;

        indices = (uint16_t *) ((uint8_t *) hud->memory_map +
                                FV_HUD_INDICES_OFFSET);

        for (i = 0; i < FV_HUD_MAX_RECTANGLES; i++) {
                indices[i * 6 + 0] = i * 4 + 0;
                indices[i * 6 + 1] = i * 4 + 3;
                indices[i * 6 + 2] = i * 4 + 1;
                indices[i * 6 + 3] = i * 4 + 1;
                indices[i * 6 + 4] = i * 4 + 3;
                indices[i * 6 + 5] = i * 4 + 2;
        }

        return hud;

error:
        fv_hud_free(hud);
        return NULL;
}

static void
fv_hud_begin_rectangles(struct fv_hud *hud,
                        int screen_width,
                        int screen_height)
{
        hud->vertex = hud->memory_map;
        hud->n_rectangles = 0;
        hud->screen_width = screen_width;
        hud->screen_height = screen_height;
}

static void
fv_hud_add_rectangle(struct fv_hud *hud,
                     int x, int y,
                     const struct fv_hud_image *image)
{
        float x1, y1, x2, y2, s1, t1, s2, t2;

        assert(hud->n_rectangles < FV_HUD_MAX_RECTANGLES);

        x1 = x * 2.0f / hud->screen_width - 1.0f;
        y1 = y * 2.0f / hud->screen_height - 1.0f;
        x2 = (x + image->w) * 2.0f / hud->screen_width - 1.0f;
        y2 = (y + image->h) * 2.0f / hud->screen_height - 1.0f;
        s1 = image->x / (float) hud->tex_width;
        t1 = image->y / (float) hud->tex_height;
        s2 = (image->x + image->w) / (float) hud->tex_width;
        t2 = (image->y + image->h) / (float) hud->tex_height;

        hud->vertex->x = x1;
        hud->vertex->y = y1;
        hud->vertex->s = s1;
        hud->vertex->t = t1;
        hud->vertex++;

        hud->vertex->x = x2;
        hud->vertex->y = y1;
        hud->vertex->s = s2;
        hud->vertex->t = t1;
        hud->vertex++;

        hud->vertex->x = x2;
        hud->vertex->y = y2;
        hud->vertex->s = s2;
        hud->vertex->t = t2;
        hud->vertex++;

        hud->vertex->x = x1;
        hud->vertex->y = y2;
        hud->vertex->s = s1;
        hud->vertex->t = t2;
        hud->vertex++;

        hud->n_rectangles++;
}

static void
fv_hud_end_rectangles(struct fv_hud *hud,
                      VkCommandBuffer command_buffer)
{
        VkDeviceSize vertices_offset = 0;

        fv_vk.vkCmdBindPipeline(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                hud->pipeline);
        fv_vk.vkCmdBindDescriptorSets(command_buffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      hud->layout,
                                      0, /* firstSet */
                                      1, /* descriptorSetCount */
                                      &hud->descriptor_set,
                                      0, /* dynamicOffsetCount */
                                      NULL /* pDynamicOffsets */);
        fv_vk.vkCmdBindVertexBuffers(command_buffer,
                                     0, /* firstBinding */
                                     1, /* bindingCount */
                                     &hud->buffer,
                                     &vertices_offset);
        fv_vk.vkCmdBindIndexBuffer(command_buffer,
                                   hud->buffer,
                                   FV_HUD_INDICES_OFFSET,
                                   VK_INDEX_TYPE_UINT16);
        fv_vk.vkCmdDrawIndexed(command_buffer,
                               hud->n_rectangles * 6,
                               1, /* instanceCount */
                               0, /* firstIndex */
                               0, /* vertexOffset */
                               0 /* firstInstance */);
}

static void
fv_hud_add_title(struct fv_hud *hud)
{
        fv_hud_add_rectangle(hud,
                             hud->screen_width / 2 - fv_hud_image_title.w / 2,
                             hud->screen_height / 2 - fv_hud_image_title.h,
                             &fv_hud_image_title);
}

void
fv_hud_paint_player_select(struct fv_hud *hud,
                           VkCommandBuffer command_buffer,
                           int screen_width,
                           int screen_height)
{
        fv_hud_begin_rectangles(hud, screen_width, screen_height);
        fv_hud_add_title(hud);
        fv_hud_add_rectangle(hud,
                             screen_width / 2 -
                             fv_hud_image_player_select.w / 2,
                             screen_height / 2 + 10,
                             &fv_hud_image_player_select);
        fv_hud_end_rectangles(hud, command_buffer);
}

void
fv_hud_paint_key_select(struct fv_hud *hud,
                        VkCommandBuffer command_buffer,
                        int screen_width,
                        int screen_height,
                        int player_num,
                        int key_num,
                        int n_players)
{
        const struct fv_hud_image *key_image = NULL;
        int x, y;

        fv_hud_begin_rectangles(hud, screen_width, screen_height);

        fv_hud_add_title(hud);

        key_image = fv_hud_key_images[key_num];

        if (n_players == 1) {
                x = (screen_width / 2 -
                     fv_hud_image_push.w / 2 -
                     key_image->w / 2);
                y = screen_height / 2 + 10;
                fv_hud_add_rectangle(hud, x, y, &fv_hud_image_push);
                fv_hud_add_rectangle(hud,
                                     x + fv_hud_image_push.w, y,
                                     key_image);
        } else {
                x = (screen_width / 4 -
                     fv_hud_image_push.w / 2 +
                     player_num % 2 * screen_width / 2);
                y = (screen_height / 4 +
                     (player_num / 2) * screen_height / 2);
                fv_hud_add_rectangle(hud,
                                     x,
                                     y - fv_hud_image_push.h,
                                     &fv_hud_image_push);
                fv_hud_add_rectangle(hud,
                                     x +
                                     (fv_hud_image_push.w - key_image->w) / 2,
                                     y,
                                     key_image);
        }


        fv_hud_end_rectangles(hud, command_buffer);
}

static void
fv_hud_add_images(struct fv_hud *hud,
                  const struct fv_hud_image * const *images,
                  int n_images,
                  int x, int y,
                  enum fv_hud_alignment alignment)
{
        int total_width = 0;
        int height = 0;
        int i;

        if (n_images <= 0)
                return;

        for (i = 0; i < n_images; i++) {
                total_width += images[i]->w;
                if (images[i]->h > height)
                        height = images[i]->h;
        }

        if (alignment == FV_HUD_ALIGNMENT_RIGHT) {
                for (i = n_images - 1; i >= 0; i--) {
                        x -= images[i]->w;
                        fv_hud_add_rectangle(hud,
                                             x,
                                             y + height / 2 - images[i]->h / 2,
                                             images[i]);
                }
        } else {
                if (alignment == FV_HUD_ALIGNMENT_CENTER)
                        x -= total_width / 2;

                for (i = 0; i < n_images; i++) {
                        fv_hud_add_rectangle(hud,
                                             x,
                                             y + height / 2 - images[i]->h / 2,
                                             images[i]);
                        x += images[i]->w;
                }
        }
}

static void
fv_hud_add_number(struct fv_hud *hud,
                  const struct fv_hud_image *symbol,
                  int value,
                  int x, int y,
                  enum fv_hud_alignment alignment)
{
        const struct fv_hud_image *images[4], *digits[FV_N_ELEMENTS(images)];
        int n_images, i;

        images[0] = symbol;

        if (value == 0) {
                images[1] = fv_hud_digit_images[0];
                n_images = 2;
        } else {
                n_images = 0;

                while (value > 0) {
                        digits[n_images++] = fv_hud_digit_images[value % 10];
                        value /= 10;
                }

                for (i = 0; i < n_images; i++)
                        images[i + 1] = digits[n_images - 1 - i];

                n_images++;
        }

        fv_hud_add_images(hud, images, n_images, x, y, alignment);
}

static void
fv_hud_add_scores(struct fv_hud *hud,
                  int screen_width,
                  int screen_height,
                  struct fv_logic *logic)
{
        int n_players = fv_logic_get_n_players(logic);

        fv_hud_add_number(hud,
                          &fv_hud_image_star,
                          fv_logic_get_score(logic, 0),
                          0, 0, /* x/y */
                          FV_HUD_ALIGNMENT_LEFT);

        if (n_players < 2)
                return;

        fv_hud_add_number(hud,
                          &fv_hud_image_star,
                          fv_logic_get_score(logic, 1),
                          screen_width,
                          0, /* y */
                          FV_HUD_ALIGNMENT_RIGHT);

        if (n_players < 3)
                return;

        fv_hud_add_number(hud,
                          &fv_hud_image_star,
                          fv_logic_get_score(logic, 2),
                          0, screen_height - fv_hud_digit_images[0]->h,
                          FV_HUD_ALIGNMENT_LEFT);

        if (n_players < 4)
                return;

        fv_hud_add_number(hud,
                          &fv_hud_image_star,
                          fv_logic_get_score(logic, 3),
                          screen_width,
                          screen_height - fv_hud_digit_images[0]->h,
                          FV_HUD_ALIGNMENT_RIGHT);
}

static void
fv_hud_add_fina_venko(struct fv_hud *hud,
                      int screen_width,
                      int screen_height,
                      float fina_venko_time)
{
        float x;

        x = fv_ease_out_bounce(fina_venko_time,
                               -fv_hud_image_fina.w,
                               screen_width / 2 - fv_hud_image_fina.w / 2 +
                               fv_hud_image_fina.w,
                               FV_HUD_FINA_VENKO_SLIDE_TIME);
        fv_hud_add_rectangle(hud,
                             x, screen_height / 2 - fv_hud_image_fina.h,
                             &fv_hud_image_fina);

        if (fina_venko_time >= FV_HUD_FINA_VENKO_SLIDE_TIME / 2.0f) {
                fina_venko_time -= FV_HUD_FINA_VENKO_SLIDE_TIME / 2.0f;

                x = fv_ease_out_bounce(fina_venko_time,
                                       screen_width,
                                       -screen_width / 2 -
                                       fv_hud_image_venko.w / 2 +
                                       30.0f,
                                       FV_HUD_FINA_VENKO_SLIDE_TIME);
                fv_hud_add_rectangle(hud,
                                     x,
                                     screen_height / 2,
                                     &fv_hud_image_venko);
        }
}


void
fv_hud_paint_game_state(struct fv_hud *hud,
                        VkCommandBuffer command_buffer,
                        int screen_width,
                        int screen_height,
                        struct fv_logic *logic)
{
        int crocodile_x, crocodile_y;
        enum fv_hud_alignment crocodile_alignment;
        int n_crocodiles;
        float time_since_fina_venko;

        fv_hud_begin_rectangles(hud, screen_width, screen_height);

        n_crocodiles = fv_logic_get_n_crocodiles(logic);

        if (fv_logic_get_n_players(logic) == 1) {
                crocodile_x = screen_width;
                crocodile_y = 0;
                crocodile_alignment = FV_HUD_ALIGNMENT_RIGHT;
        } else {
                crocodile_x = screen_width / 2;
                crocodile_y = 0;
                crocodile_alignment = FV_HUD_ALIGNMENT_CENTER;
        }

        fv_hud_add_number(hud,
                          &fv_hud_image_crocodile,
                          n_crocodiles,
                          crocodile_x, crocodile_y,
                          crocodile_alignment);

        fv_hud_add_scores(hud,
                          screen_width,
                          screen_height,
                          logic);

        if (fv_logic_get_state(logic) == FV_LOGIC_STATE_FINA_VENKO) {
                time_since_fina_venko =
                        fv_logic_get_time_since_fina_venko(logic);
                fv_hud_add_fina_venko(hud,
                                      screen_width, screen_height,
                                      time_since_fina_venko);
        }

        fv_hud_end_rectangles(hud, command_buffer);
}

void
fv_hud_free(struct fv_hud *hud)
{
        if (hud->memory_map) {
                fv_vk.vkUnmapMemory(hud->vk_data->device,
                                    hud->memory);
        }
        if (hud->memory) {
                fv_vk.vkFreeMemory(hud->vk_data->device,
                                   hud->memory,
                                   NULL /* allocator */);
        }
        if (hud->buffer) {
                fv_vk.vkDestroyBuffer(hud->vk_data->device,
                                      hud->buffer,
                                      NULL /* allocator */);
        }
        if (hud->descriptor_set) {
                fv_vk.vkFreeDescriptorSets(hud->vk_data->device,
                                           hud->vk_data->descriptor_pool,
                                           1, /* descriptorSetCount */
                                           &hud->descriptor_set);
        }
        if (hud->sampler) {
                fv_vk.vkDestroySampler(hud->vk_data->device,
                                       hud->sampler,
                                       NULL /* allocator */);
        }
        if (hud->texture_view) {
                fv_vk.vkDestroyImageView(hud->vk_data->device,
                                         hud->texture_view,
                                         NULL /* allocator */);
        }
        if (hud->texture_image) {
                fv_vk.vkDestroyImage(hud->vk_data->device,
                                     hud->texture_image,
                                     NULL /* allocator */);
        }
        if (hud->texture_memory) {
                fv_vk.vkFreeMemory(hud->vk_data->device,
                                   hud->texture_memory,
                                   NULL /* allocator */);
        }

        fv_free(hud);
}
