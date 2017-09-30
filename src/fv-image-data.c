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

#include "fv-image-data.h"
#include "fv-data.h"
#include "fv-util.h"
#include "fv-error-message.h"
#include "fv-allocate-store.h"

#define STB_IMAGE_IMPLEMENTATION 1
#define STB_IMAGE_STATIC 1
#include "stb_image.h"

struct image_details {
        int full_width, width, height;
        int miplevels;
        VkFormat format;
};

static const char *
image_filenames[] = {
#include "data/fv-image-data-files.h"
};

struct fv_image_data {
        VkCommandBuffer command_buffer;
        const struct fv_vk_data *vk_data;
        struct image_details images[FV_N_ELEMENTS(image_filenames)];
        VkBuffer buffers[FV_N_ELEMENTS(image_filenames)];
        int offsets[FV_N_ELEMENTS(image_filenames)];
        VkDeviceMemory memory;
};

static int
count_miplevels(int width, int height)
{
        int miplevels = 1;

        while (width > 1 || height > 1) {
                width /= 2;
                height /= 2;
                miplevels++;
        }

        return miplevels;
}

static bool
components_to_format(int components,
                     VkFormat *format)
{
        switch (components) {
        case 3:
                *format = VK_FORMAT_R8G8B8_UNORM;
                return true;
        case 4:
                *format = VK_FORMAT_R8G8B8A8_UNORM;
                return true;
        default:
                return false;
        }
}

static int
format_to_components(VkFormat format)
{
        switch (format) {
        case VK_FORMAT_R8G8B8_UNORM:
                return 3;
        default:
                return 4;
        }
}

static size_t
get_next_image_offset(int width, int height, VkFormat format)
{
        int components = format_to_components(format);
        size_t size = components * width * height;
        size_t ma, mb;

        /* The buffer offsets must all be aligned to 4 and the size of
         * the format according to the Vulkan spec. So we’ll pick an
         * alignment that is the least common multiple of the two */
        ma = 4;
        mb = components;

        while (ma != mb) {
                if (ma < mb)
                        ma += 4;
                else
                        mb += components;
        }

        if (size % ma)
                size += ma - (size % ma);

        return size;
}

static size_t
get_buffer_size(const struct image_details *image)
{
        int miplevels = image->miplevels;
        int width = image->width;
        int height = image->height;
        size_t size = 0;
        int i;

        for (i = 0; i < miplevels; i++) {
                size += get_next_image_offset(width, height, image->format);
                width = MAX(width / 2, 1);
                height = MAX(height / 2, 1);
        }

        return size;
}

static uint8_t *
load_image(const char *name,
           int expected_width,
           int expected_height,
           VkFormat expected_format)
{
        char *filename = fv_data_get_filename(name);
        int components;
        int width, height;
        VkFormat format;
        uint8_t *data;

        if (filename == NULL) {
                fv_error_message("Failed to get filename for %s", name);
                return NULL;
        }

        data = stbi_load(filename,
                         &width, &height,
                         &components,
                         0 /* components */);

        if (data == NULL) {
                fv_error_message("%s: %s",
                                 filename,
                                 stbi_failure_reason());
                fv_free(filename);
                return NULL;
        }

        fv_free(filename);

        if (!components_to_format(components, &format) ||
            format != expected_format ||
            width != expected_width ||
            height != expected_height) {
                fv_free(data);
                return NULL;
        }

        return data;
}

static bool
filename_is_mipng(const char *name)
{
        int len = strlen(name);

        return len >= 6 && !strcmp(name + len - 6, ".mipng");
}

static bool
load_info(const struct fv_vk_data *vk_data,
          const char *name,
          struct image_details *image)
{
        char *filename = fv_data_get_filename(name);
        int components, res;
        bool is_mipng = filename_is_mipng(name);

        if (filename == NULL) {
                fv_error_message("Failed to get filename for %s", name);
                return false;
        }

        res = stbi_info(filename,
                        &image->full_width, &image->height,
                        &components);

        if (!res) {
                fv_error_message("%s: %s",
                                 filename,
                                 stbi_failure_reason());
                fv_free(filename);
                return false;
        }

        fv_free(filename);

        if (is_mipng) {
                /* Width must be an even number to hold the mipmaps */
                if (image->full_width & 1)
                        return false;

                image->width = image->full_width / 2;
                image->miplevels = count_miplevels(image->width, image->height);
        } else {
                image->width = image->full_width;
                image->miplevels = 1;
        }

        return components_to_format(components, &image->format);
}

static bool
create_buffers(struct fv_image_data *data)
{
        const struct fv_vk_data *vk_data = data->vk_data;
        VkResult res;
        int i;

        for (i = 0; i < FV_N_ELEMENTS(data->images); i++) {
                VkBufferCreateInfo buffer_create_info = {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                        .size = get_buffer_size(data->images + i),
                        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
                };
                res = fv_vk.vkCreateBuffer(vk_data->device,
                                           &buffer_create_info,
                                           NULL, /* allocator */
                                           data->buffers + i);

                if (res != VK_SUCCESS) {
                        for (--i; i >= 0; --i) {
                                fv_vk.vkDestroyBuffer(vk_data->device,
                                                      data->buffers[i],
                                                      NULL /* allocator */);
                        }

                        return false;
                }
        }

        return true;
}

static void
copy_image(const struct image_details *image,
           uint8_t *dst,
           const uint8_t *src)
{
        int components = format_to_components(image->format);
        int w = image->width;
        int h = image->height;
        int miplevels = image->miplevels;
        int x = 0;
        int y = 0;
        bool go_right = true;
        int i, j;

        for (i = 0; i < miplevels; i++) {
                for (j = 0; j < h; j++) {
                        memcpy(dst + j * w * components,
                               src + (x + (y + j) * image->full_width) *
                               components,
                               w * components);
                }

                dst += get_next_image_offset(w, h, image->format);

                if (go_right)
                        x += w;
                else
                        y += h;
                go_right = !go_right;

                w = MAX(w / 2, 1);
                h = MAX(h / 2, 1);
        }
}

static bool
copy_images(struct fv_image_data *data)
{
        const struct image_details *image;
        VkResult res;
        bool ret = true;
        uint8_t *mapped_memory;
        uint8_t *pixels;
        int i;

        res = fv_vk.vkMapMemory(data->vk_data->device,
                                data->memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                (void **) &mapped_memory);
        if (res != VK_SUCCESS)
                return false;

        for (i = 0; i < FV_N_ELEMENTS(data->images); i++) {
                image = data->images + i;
                pixels = load_image(image_filenames[i],
                                    image->full_width,
                                    image->height,
                                    image->format);

                if (pixels == NULL) {
                        ret = false;
                        break;
                }

                copy_image(image,
                           mapped_memory + data->offsets[i],
                           pixels);

                fv_free(pixels);
        }

        fv_vk.vkUnmapMemory(data->vk_data->device,
                            data->memory);

        return ret;
}

struct fv_image_data *
fv_image_data_new(const struct fv_vk_data *vk_data,
                  VkCommandBuffer command_buffer)
{
        struct fv_image_data *data;
        int i;
        bool res;
        VkResult vres;

        data = fv_alloc(sizeof *data);

        data->vk_data = vk_data;
        data->command_buffer = command_buffer;

        /* Get the size of all the images */
        for (i = 0; i < FV_N_ELEMENTS(data->images); i++) {
                res = load_info(vk_data, image_filenames[i], data->images + i);

                if (!res)
                        goto error_data;
        }

        res = create_buffers(data);
        if (!res)
                goto error_data;

        vres = fv_allocate_store_buffer(vk_data,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                        FV_N_ELEMENTS(data->buffers),
                                        data->buffers,
                                        &data->memory,
                                        data->offsets);
        if (vres != VK_SUCCESS)
                goto error_buffers;

        if (!copy_images(data))
                goto error_memory;

        return data;

error_memory:
        fv_vk.vkFreeMemory(vk_data->device,
                           data->memory,
                           NULL /* allocator */);
error_buffers:
        for (i = 0; i < FV_N_ELEMENTS(data->images); i++)
                fv_vk.vkDestroyBuffer(vk_data->device,
                                      data->buffers[i],
                                      NULL /* allocator */);
error_data:
        fv_free(data);
        return NULL;
}

void
fv_image_data_get_size(const struct fv_image_data *data,
                       enum fv_image_data_image image,
                       int *width,
                       int *height)
{
        *width = data->images[image].width;
        *height = data->images[image].height;
}

int
fv_image_data_get_miplevels(const struct fv_image_data *data,
                            enum fv_image_data_image image)
{
        return data->images[image].miplevels;
}

VkFormat
fv_image_data_get_format(const struct fv_image_data *data,
                         enum fv_image_data_image image)
{
        return data->images[image].format;
}

static void
copy_image_from_buffer(const struct fv_image_data *data,
                       VkImage image,
                       enum fv_image_data_image image_num,
                       int array_layer)
{
        const struct image_details *image_details = data->images + image_num;
        int w = image_details->width;
        int h = image_details->height;
        int miplevels = image_details->miplevels;
        int offset = 0;
        VkBufferImageCopy *regions = alloca(sizeof *regions * miplevels);
        VkBufferImageCopy *region;
        int i;

        for (i = 0; i < miplevels; i++) {
                region = regions + i;

                region->bufferOffset = offset;
                region->bufferRowLength = 0;
                region->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region->imageSubresource.mipLevel = i;
                region->imageSubresource.baseArrayLayer = array_layer;
                region->imageSubresource.layerCount = 1;
                memset(&region->imageOffset, 0, sizeof region->imageOffset);
                region->imageExtent.width = w;
                region->imageExtent.height = h;
                region->imageExtent.depth = 1;

                offset += get_next_image_offset(w, h, image_details->format);
                w = MAX(w / 2, 1);
                h = MAX(h / 2, 1);
        }

        fv_vk.vkCmdCopyBufferToImage(data->command_buffer,
                                     data->buffers[image_num],
                                     image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     miplevels,
                                     regions);
}

static VkResult
create_image(const struct fv_image_data *data,
             int n_images,
             const enum fv_image_data_image *image_nums,
             VkImage *image_out,
             VkDeviceMemory *memory_out)
{
        const struct image_details *image_details =
                data->images + image_nums[0];
        const struct image_details *other_image;
        VkImage image;
        int i, image_index;
        int miplevels = image_details->miplevels;
        VkResult res;

        /* Verify that all of the images have the same size and
         * format */
        for (i = 1; i < n_images; i++) {
                other_image = data->images + image_nums[i];

                if (image_details->format != other_image->format ||
                    image_details->width != other_image->width ||
                    image_details->height != other_image->height ||
                    image_details->miplevels != other_image->miplevels) {
                        fv_error_message("Images in texture array do not all "
                                         "have the same size or format");
                        return VK_ERROR_FORMAT_NOT_SUPPORTED;
                }
        }

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = image_details->format,
                .extent = {
                        .width = image_details->width,
                        .height = image_details->height,
                        .depth = 1
                },
                .mipLevels = miplevels,
                .arrayLayers = n_images,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = (VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        res = fv_vk.vkCreateImage(data->vk_data->device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &image);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkImage");
                return res;
        }

        res = fv_allocate_store_image(data->vk_data,
                                      0, /* memory_type_flags */
                                      1, /* n_images */
                                      &image,
                                      memory_out,
                                      NULL /* memory_type_index_out */);
        if (res != VK_SUCCESS) {
                fv_vk.vkDestroyImage(data->vk_data->device,
                                     image,
                                     NULL /* allocator */);
                return res;
        }

        VkImageMemoryBarrier image_memory_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = miplevels,
                        .layerCount = n_images
                }
        };
        fv_vk.vkCmdPipelineBarrier(data->command_buffer,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0, /* dependencyFlags */
                                   0, /* memoryBarrierCount */
                                   NULL, /* pMemoryBarriers */
                                   0, /* bufferMemoryBarrierCount */
                                   NULL, /* pBufferMemoryBarriers */
                                   1, /* imageMemoryBarrierCount */
                                   &image_memory_barrier);

        for (image_index = 0; image_index < n_images; image_index++) {
                copy_image_from_buffer(data,
                                       image,
                                       image_nums[image_index],
                                       image_index);
        }

        image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        image_memory_barrier.oldLayout =
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_memory_barrier.newLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        fv_vk.vkCmdPipelineBarrier(data->command_buffer,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   0, /* dependencyFlags */
                                   0, /* memoryBarrierCount */
                                   NULL, /* pMemoryBarriers */
                                   0, /* bufferMemoryBarrierCount */
                                   NULL, /* pBufferMemoryBarriers */
                                   1, /* imageMemoryBarrierCount */
                                   &image_memory_barrier);

        *image_out = image;

        return VK_SUCCESS;
}

VkResult
fv_image_data_create_image_2d(const struct fv_image_data *data,
                              enum fv_image_data_image image_num,
                              VkImage *image_out,
                              VkDeviceMemory *memory_out)
{
        return create_image(data,
                            1, /* n_images */
                            &image_num,
                            image_out,
                            memory_out);
}

VkResult
fv_image_data_create_image_2d_array(const struct fv_image_data *data,
                                    int n_images,
                                    const enum fv_image_data_image *image_nums,
                                    VkImage *image_out,
                                    VkDeviceMemory *memory_out)
{
        return create_image(data,
                            n_images, /* n_images */
                            image_nums,
                            image_out,
                            memory_out);
}

void
fv_image_data_free(struct fv_image_data *data)
{
        int i;

        for (i = 0; i < FV_N_ELEMENTS(data->images); i++)
                fv_vk.vkDestroyBuffer(data->vk_data->device,
                                      data->buffers[i],
                                      NULL /* allocator */);

        fv_vk.vkFreeMemory(data->vk_data->device,
                           data->memory,
                           NULL /* allocator */);

        fv_free(data);
}
