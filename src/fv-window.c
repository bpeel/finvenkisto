/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014, 2015, 2017 Neil Roberts
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

#include <SDL.h>
#include <SDL_syswm.h>
#include <stdbool.h>
#include <assert.h>

#include "fv-window.h"
#include "fv-util.h"
#include "fv-error-message.h"
#include "fv-allocate-store.h"

struct swapchain_image {
        VkImage image;
        VkImageView image_view;
        VkFramebuffer framebuffer;
};

struct fv_window {
        /* Permanant vulkan resources */
        struct fv_vk_data vk_data;
        VkInstance vk_instance;
        VkFormat vk_depth_format;
        VkFence vk_fence;
        VkSurfaceKHR vk_surface;
        VkSemaphore vk_semaphore;
        VkFormat vk_surface_format;
        VkPresentModeKHR vk_present_mode;

        /* Resources that are recreated lazily whenever the
         * framebuffer size changes.
         */
        struct {
                VkSwapchainKHR swapchain;
                uint32_t n_swapchain_images;
                struct swapchain_image *swapchain_images;
                VkImage depth_image;
                VkDeviceMemory depth_image_memory;
                VkImageView depth_image_view;
                VkSurfaceCapabilitiesKHR caps;
                VkExtent2D extent;
        } vk_fb;

        SDL_Window *window;
        SDL_SysWMinfo window_info;

        bool is_fullscreen;

        bool libvulkan_loaded;
        bool sdl_inited;

        uint32_t swapchain_image_index;
};

void
fv_window_toggle_fullscreen(struct fv_window *window)
{
        int display_index;
        SDL_DisplayMode mode;

        display_index = SDL_GetWindowDisplayIndex(window->window);

        if (display_index == -1)
                return;

        if (SDL_GetDesktopDisplayMode(display_index, &mode) == -1)
                return;

        SDL_SetWindowDisplayMode(window->window, &mode);

        window->is_fullscreen = !window->is_fullscreen;

        SDL_SetWindowFullscreen(window->window, window->is_fullscreen);
}

static void
destroy_swapchain_image(struct fv_window *window,
                        struct swapchain_image *swapchain_image)
{
        if (swapchain_image->framebuffer) {
                fv_vk.vkDestroyFramebuffer(window->vk_data.device,
                                           swapchain_image->framebuffer,
                                           NULL /* allocator */);
        }
        if (swapchain_image->image_view) {
                fv_vk.vkDestroyImageView(window->vk_data.device,
                                         swapchain_image->image_view,
                                         NULL /* allocator */);
        }
}

static void
destroy_framebuffer_resources(struct fv_window *window)
{
        int i;

        if (window->vk_fb.depth_image_view)
                fv_vk.vkDestroyImageView(window->vk_data.device,
                                         window->vk_fb.depth_image_view,
                                         NULL /* allocator */);
        if (window->vk_fb.depth_image_memory)
                fv_vk.vkFreeMemory(window->vk_data.device,
                                   window->vk_fb.depth_image_memory,
                                   NULL /* allocator */);
        if (window->vk_fb.depth_image)
                fv_vk.vkDestroyImage(window->vk_data.device,
                                     window->vk_fb.depth_image,
                                     NULL /* allocator */);
        if (window->vk_fb.swapchain_images) {
                for (i = 0; i < window->vk_fb.n_swapchain_images; i++) {
                        destroy_swapchain_image(window,
                                                window->vk_fb.swapchain_images +
                                                i);
                }
                fv_free(window->vk_fb.swapchain_images);
        }
        if (window->vk_fb.swapchain)
                fv_vk.vkDestroySwapchainKHR(window->vk_data.device,
                                            window->vk_fb.swapchain,
                                            NULL /* allocator */);

        memset(&window->vk_fb, 0, sizeof window->vk_fb);
}

void
fv_window_resized(struct fv_window *window)
{
        /* If the window size is determined by the swap chain size
         * then we’ll destroy the fb resources in order to trigger it
         * to recreate them at the right size. Otherwise this should
         * be recognised when we try to acquire an out-of-date buffer.
         */
        if (window->vk_fb.swapchain &&
            window->vk_fb.caps.currentExtent.width == 0xffffffff) {
                destroy_framebuffer_resources(window);
        }
}

static bool
create_swapchain_image(struct fv_window *window,
                       struct swapchain_image *swapchain_image)
{
        VkResult res;

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapchain_image->image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = window->vk_surface_format,
                .components = {
                        .r = VK_COMPONENT_SWIZZLE_R,
                        .g = VK_COMPONENT_SWIZZLE_G,
                        .b = VK_COMPONENT_SWIZZLE_B,
                        .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = fv_vk.vkCreateImageView(window->vk_data.device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &swapchain_image->image_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating image view");
                goto error;
        }

        VkImageView attachments[] = {
                swapchain_image->image_view,
                window->vk_fb.depth_image_view
        };
        VkFramebufferCreateInfo framebuffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = window->vk_data.render_pass,
                .attachmentCount = FV_N_ELEMENTS(attachments),
                .pAttachments = attachments,
                .width = window->vk_fb.extent.width,
                .height = window->vk_fb.extent.height,
                .layers = 1
        };
        res = fv_vk.vkCreateFramebuffer(window->vk_data.device,
                                        &framebuffer_create_info,
                                        NULL, /* allocator */
                                        &swapchain_image->framebuffer);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating framebuffer");
                goto error;
        }

        return true;

error:
        return false;
}

static bool
create_swapchain_images(struct fv_window *window)
{
        VkResult res;
        VkImage *images;
        uint32_t n_images;
        int i;

        res = fv_vk.vkGetSwapchainImagesKHR(window->vk_data.device,
                                            window->vk_fb.swapchain,
                                            &n_images,
                                            NULL /* images */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting swapchain images");
                goto error;
        }
        images = alloca(sizeof *images * n_images);
        res = fv_vk.vkGetSwapchainImagesKHR(window->vk_data.device,
                                            window->vk_fb.swapchain,
                                            &n_images,
                                            images);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting swapchain images");
                goto error;
        }

        window->vk_fb.swapchain_images =
                fv_calloc(sizeof *window->vk_fb.swapchain_images * n_images);
        window->vk_fb.n_swapchain_images = n_images;

        for (i = 0; i < n_images; i++) {
                window->vk_fb.swapchain_images[i].image = images[i];
                if (!create_swapchain_image(window,
                                            window->vk_fb.swapchain_images + i))
                        goto error;
        }

        return true;

error:
        return false;
}

static void
get_fb_extent(struct fv_window *window)
{
        int w, h;

        /* This value is used when the window size is determined by
         * the swap chain size, such as on Wayland. In that case will
         * ask SDL for the right size. */
        if (window->vk_fb.caps.currentExtent.width == 0xffffffff) {
                SDL_GetWindowSize(window->window, &w, &h);
                window->vk_fb.extent.width = w;
                window->vk_fb.extent.height = h;
        } else {
                window->vk_fb.extent = window->vk_fb.caps.currentExtent;
        }
}

static bool
create_framebuffer_resources(struct fv_window *window)
{
        VkPhysicalDevice physical_device = window->vk_data.physical_device;
        VkSurfaceCapabilitiesKHR *caps = &window->vk_fb.caps;
        VkSurfaceKHR surface = window->vk_surface;
        VkResult res;

        res = fv_vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device,
                                                              surface,
                                                              caps);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting device surface caps");
                goto error;
        }

        get_fb_extent(window);

        VkSwapchainCreateInfoKHR swapchain_create_info = {
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                .surface = surface,
                .minImageCount = MAX(caps->minImageCount, 2),
                .imageFormat = window->vk_surface_format,
                .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
                .imageExtent = window->vk_fb.extent,
                .imageArrayLayers = 1,
                .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 1,
                .pQueueFamilyIndices =
                (uint32_t[]) { window->vk_data.queue_family },
                .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                .presentMode = window->vk_present_mode,
                .clipped = VK_TRUE
        };
        res = fv_vk.vkCreateSwapchainKHR(window->vk_data.device,
                                         &swapchain_create_info,
                                         NULL, /* allocator */
                                         &window->vk_fb.swapchain);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating swapchain");
                goto error;
        }

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = window->vk_depth_format,
                .extent = {
                        .width = window->vk_fb.extent.width,
                        .height = window->vk_fb.extent.height,
                        .depth = 1
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        res = fv_vk.vkCreateImage(window->vk_data.device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &window->vk_fb.depth_image);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating depth image");
                goto error;
        }

        res = fv_allocate_store_image(&window->vk_data,
                                      0, /* memory_type_flags */
                                      1, /* n_images */
                                      &window->vk_fb.depth_image,
                                      &window->vk_fb.depth_image_memory,
                                      NULL /* memory_type_index */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating depthbuffer memory");
                goto error;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = window->vk_fb.depth_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = window->vk_depth_format,
                .components = {
                        .r = VK_COMPONENT_SWIZZLE_R,
                        .g = VK_COMPONENT_SWIZZLE_G,
                        .b = VK_COMPONENT_SWIZZLE_B,
                        .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = fv_vk.vkCreateImageView(window->vk_data.device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &window->vk_fb.depth_image_view);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating depth-stencil image view");
                goto error;
        }

        if (!create_swapchain_images(window))
                goto error;

        return true;

error:
        destroy_framebuffer_resources(window);

        return false;
}

static bool
acquire_image(struct fv_window *window)
{
        VkResult res;
        int i;

        for (i = 0; i < 2; i++) {
                if (window->vk_fb.swapchain == NULL &&
                    !create_framebuffer_resources(window)) {
                        return false;
                }

                res = fv_vk.vkAcquireNextImageKHR(window->vk_data.device,
                                                  window->vk_fb.swapchain,
                                                  UINT64_MAX,
                                                  window->vk_semaphore,
                                                  VK_NULL_HANDLE, /* fence */
                                                  &window->
                                                  swapchain_image_index);
                if (i == 0 &&
                    (res == VK_ERROR_OUT_OF_DATE_KHR ||
                     res == VK_SUBOPTIMAL_KHR)) {
                        /* This will probably happen if the window is
                         * resized while we are waiting. Try
                         * recreating the resources with the right
                         * size. */
                        destroy_framebuffer_resources(window);
                } else if (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR) {
                        return true;
                } else {
                        fv_error_message("Error getting swapchain image 0x%x",
                                         res);
                        return false;
                }
        }

        assert(false);
}

bool
fv_window_begin_paint(struct fv_window *window,
                      bool need_clear)
{
        struct swapchain_image *swapchain_image;
        const VkExtent2D *extent;
        VkResult res;

        if (!acquire_image(window))
                return false;

        extent = &window->vk_fb.extent;

        swapchain_image = (window->vk_fb.swapchain_images +
                           window->swapchain_image_index);

        VkCommandBufferBeginInfo begin_command_buffer_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        res = fv_vk.vkBeginCommandBuffer(window->vk_data.command_buffer,
                                         &begin_command_buffer_info);
        if (res != VK_SUCCESS)
                return false;

        VkClearValue clear_values[] = {
                [1] = {
                        .depthStencil = {
                                .depth = 1.0f,
                                .stencil = 0
                        }
                }
        };
        VkRenderPassBeginInfo render_pass_begin_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = window->vk_data.render_pass,
                .framebuffer = swapchain_image->framebuffer,
                .renderArea = {
                        .offset = { 0, 0 },
                        .extent = *extent
                },
                .clearValueCount = FV_N_ELEMENTS(clear_values),
                .pClearValues = clear_values
        };
        fv_vk.vkCmdBeginRenderPass(window->vk_data.command_buffer,
                                   &render_pass_begin_info,
                                   VK_SUBPASS_CONTENTS_INLINE);

        if (need_clear) {
                VkClearAttachment color_clear_attachment = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .colorAttachment = 0,
                        .clearValue = {
                                .color = {
                                        .float32 = { 0.0f, 0.0f, 0.0f, 0.0f }
                                }
                        },
                };
                VkClearRect color_clear_rect = {
                        .rect = {
                                .offset = { 0, 0 },
                                .extent = *extent
                        },
                        .baseArrayLayer = 0,
                        .layerCount = 1
                };
                fv_vk.vkCmdClearAttachments(window->vk_data.command_buffer,
                                            1, /* attachmentCount */
                                            &color_clear_attachment,
                                            1,
                                            &color_clear_rect);
        }

        VkRect2D scissor = {
                .offset = { .x = 0, .y = 0 },
                .extent = *extent
        };
        fv_vk.vkCmdSetScissor(window->vk_data.command_buffer,
                              0, /* firstScissor */
                              1, /* scissorCount */
                              &scissor);

        return true;
}

bool
fv_window_end_paint(struct fv_window *window)
{
        VkResult res;

        fv_vk.vkCmdEndRenderPass(window->vk_data.command_buffer);

        res = fv_vk.vkEndCommandBuffer(window->vk_data.command_buffer);
        if (res != VK_SUCCESS)
                return false;

        fv_vk.vkResetFences(window->vk_data.device,
                            1, /* fenceCount */
                            &window->vk_fence);

        VkSubmitInfo submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &window->vk_data.command_buffer,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = (VkSemaphore[]) { window->vk_semaphore },
                .pWaitDstStageMask =
                (VkPipelineStageFlagBits[])
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT }
        };
        res = fv_vk.vkQueueSubmit(window->vk_data.queue,
                                  1, /* submitCount */
                                  &submit_info,
                                  window->vk_fence);
        if (res != VK_SUCCESS)
                return false;

        res = fv_vk.vkWaitForFences(window->vk_data.device,
                                    1, /* fenceCount */
                                    &window->vk_fence,
                                    VK_TRUE, /* waitAll */
                                    UINT64_MAX);
        if (res != VK_SUCCESS)
                return false;

        VkPresentInfoKHR present_info = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .swapchainCount = 1,
                .pSwapchains = (VkSwapchainKHR[]) { window->vk_fb.swapchain },
                .pImageIndices = (uint32_t[]) { window->swapchain_image_index },
        };
        res = fv_vk.vkQueuePresentKHR(window->vk_data.queue,
                                      &present_info);
        if (res != VK_SUCCESS) {
                fv_error_message("Error presenting image");
                return false;
        }

        return true;
}

static int
find_queue_family(struct fv_window *window,
                  VkPhysicalDevice physical_device)
{
        VkQueueFamilyProperties *queues;
        uint32_t count = 0;
        uint32_t i;
        VkBool32 supported;

        fv_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       NULL /* queues */);

        queues = fv_alloc(sizeof *queues * count);

        fv_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       queues);

        for (i = 0; i < count; i++) {
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 ||
                    queues[i].queueCount < 1)
                        continue;

                supported = false;
                fv_vk.vkGetPhysicalDeviceSurfaceSupportKHR(physical_device,
                                                           i,
                                                           window->vk_surface,
                                                           &supported);
                if (supported)
                        break;
        }

        fv_free(queues);

        if (i >= count)
                return -1;
        else
                return i;
}

static VkFormat
get_depth_format(struct fv_window *window)
{
        /* According to the spec at least one of these formats must be
         * supported for depth so we'll just try them both until one
         * of them works.
         */
        static const VkFormat formats[] = {
                VK_FORMAT_X8_D24_UNORM_PACK32,
                VK_FORMAT_D32_SFLOAT
        };
        VkFormatProperties format_properties;
        VkPhysicalDevice physical_device = window->vk_data.physical_device;
        int i;

        for (i = 0; i < FV_N_ELEMENTS(formats); i++) {
                fv_vk.vkGetPhysicalDeviceFormatProperties(physical_device,
                                                          formats[i],
                                                          &format_properties);
                if ((format_properties.optimalTilingFeatures &
                     VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
                        return formats[i];
        }

        assert(false);
}

static void
deinit_vk(struct fv_window *window)
{
        if (window->vk_fence) {
                fv_vk.vkDestroyFence(window->vk_data.device,
                                     window->vk_fence,
                                     NULL /* allocator */);
                window->vk_fence = NULL;
        }
        if (window->vk_data.render_pass) {
                fv_vk.vkDestroyRenderPass(window->vk_data.device,
                                          window->vk_data.render_pass,
                                          NULL /* allocator */);
                window->vk_data.render_pass = NULL;
        }
        if (window->vk_data.descriptor_pool) {
                fv_vk.vkDestroyDescriptorPool(window->vk_data.device,
                                              window->vk_data.descriptor_pool,
                                              NULL /* allocator */);
                window->vk_data.descriptor_pool = NULL;
        }
        if (window->vk_data.command_buffer) {
                fv_vk.vkFreeCommandBuffers(window->vk_data.device,
                                           window->vk_data.command_pool,
                                           1, /* commandBufferCount */
                                           &window->vk_data.command_buffer);
                window->vk_data.command_buffer = NULL;
        }
        if (window->vk_data.command_pool) {
                fv_vk.vkDestroyCommandPool(window->vk_data.device,
                                           window->vk_data.command_pool,
                                           NULL /* allocator */);
                window->vk_data.command_pool = NULL;
        }
        if (window->vk_semaphore) {
                fv_vk.vkDestroySemaphore(window->vk_data.device,
                                         window->vk_semaphore,
                                         NULL /* allocator */);
                window->vk_semaphore = NULL;
        }
        if (window->vk_data.device) {
                fv_vk.vkDestroyDevice(window->vk_data.device,
                                      NULL /* allocator */);
                window->vk_data.device = NULL;
        }
        if (window->vk_surface) {
                fv_vk.vkDestroySurfaceKHR(window->vk_instance,
                                          window->vk_surface,
                                          NULL /* allocator */);
                window->vk_surface = NULL;
        }
        if (window->vk_instance) {
                fv_vk.vkDestroyInstance(window->vk_instance,
                                        NULL /* allocator */);
                window->vk_instance = NULL;
        }
}

static bool
check_device_extension(struct fv_window *window,
                       VkPhysicalDevice physical_device,
                       const char *extension)
{
        VkExtensionProperties *extensions;
        uint32_t count;
        VkResult res;
        int i;

        res = fv_vk.vkEnumerateDeviceExtensionProperties(physical_device,
                                                         NULL, /* layerName */
                                                         &count,
                                                         NULL /* properties */);
        if (res != VK_SUCCESS)
                return false;

        extensions = alloca(sizeof *extensions * count);

        res = fv_vk.vkEnumerateDeviceExtensionProperties(physical_device,
                                                         NULL, /* layerName */
                                                         &count,
                                                         extensions);
        if (res != VK_SUCCESS)
                return false;

        for (i = 0; i < count; i++) {
                if (!strcmp(extensions[i].extensionName, extension))
                        return true;
        }

        return false;
}

static bool
check_physical_device_surface_capabilities(struct fv_window *window,
                                           VkPhysicalDevice physical_device)
{
        VkSurfaceCapabilitiesKHR caps;
        VkSurfaceKHR surface = window->vk_surface;
        VkResult res;

        res = fv_vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device,
                                                              surface,
                                                              &caps);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting device surface caps");
                return false;
        }

        if (caps.maxImageCount != 0 && caps.maxImageCount < 2)
                return false;
        if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
                return false;
        if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
                return false;
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
                return false;

        return true;
}

static bool
find_physical_device(struct fv_window *window)
{
        VkResult res;
        uint32_t count;
        VkPhysicalDevice *devices;
        int i, queue_family;

        res = fv_vk.vkEnumeratePhysicalDevices(window->vk_instance,
                                               &count,
                                               NULL);
        if (res != VK_SUCCESS) {
                fv_error_message("Error enumerating VkPhysicalDevices");
                return false;
        }

        devices = alloca(count * sizeof *devices);

        res = fv_vk.vkEnumeratePhysicalDevices(window->vk_instance,
                                               &count,
                                               devices);
        if (res != VK_SUCCESS) {
                fv_error_message("Error enumerating VkPhysicalDevices");
                return false;
        }

        for (i = 0; i < count; i++) {
                if (!check_device_extension(window,
                                            devices[i],
                                            VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                        continue;

                queue_family = find_queue_family(window, devices[i]);
                if (queue_family == -1)
                        continue;

                if (!check_physical_device_surface_capabilities(window,
                                                                devices[i]))
                        continue;

                window->vk_data.physical_device = devices[i];
                window->vk_data.queue_family = queue_family;

                return true;
        }

        fv_error_message("No suitable device and queue family found");
        return false;
}

static bool
find_surface_format(struct fv_window *window)
{
        VkPhysicalDevice physical_device = window->vk_data.physical_device;
        VkSurfaceFormatKHR *formats;
        uint32_t count = 0;
        VkResult res;
        int i;

        res = fv_vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device,
                                                         window->vk_surface,
                                                         &count,
                                                         NULL /* formats */);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported surface formats");
                return false;
        }

        formats = alloca(sizeof *formats * count);

        res = fv_vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device,
                                                         window->vk_surface,
                                                         &count,
                                                         formats);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported surface formats");
                return false;
        }

        for (i = 0; i < count; i++) {
                switch (formats[i].format) {
                case VK_FORMAT_B8G8R8A8_UNORM:
                case VK_FORMAT_R8G8B8A8_UNORM:
                        window->vk_surface_format = formats[i].format;
                        return true;
                default:
                        continue;
                }
        }

        fv_error_message("No suitable surface format found");
        return false;
}

static bool
find_present_mode(struct fv_window *window)
{
        static const VkPresentModeKHR mode_preference[] = {
                VK_PRESENT_MODE_MAILBOX_KHR,
                VK_PRESENT_MODE_FIFO_KHR
        };
        VkPhysicalDevice physical_device = window->vk_data.physical_device;
        VkPresentModeKHR *present_modes;
        int chosen_preference = -1;
        VkSurfaceKHR surface = window->vk_surface;
        uint32_t count = 0;
        VkResult res;
        int i, j;

        res = fv_vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                                              surface,
                                                              &count,
                                                              NULL);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported present modes");
                return false;
        }

        present_modes = alloca(sizeof *present_modes * count);

        res = fv_vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device,
                                                              surface,
                                                              &count,
                                                              present_modes);
        if (res != VK_SUCCESS) {
                fv_error_message("Error getting supported present modes");
                return false;
        }

        for (i = 0; i < count; i++) {
                for (j = 0; j < FV_N_ELEMENTS(mode_preference); j++) {
                        if (mode_preference[j] == present_modes[i]) {
                                if (j > chosen_preference)
                                        chosen_preference = j;
                                break;
                        }
                }
        }

        if (chosen_preference == -1) {
                fv_error_message("No suitable present mode found");
                return false;
        }

        window->vk_present_mode = mode_preference[chosen_preference];
        return true;
}

#ifdef SDL_VIDEO_DRIVER_X11
static bool
create_vk_surface_x11(struct fv_window *window)
{
        VkResult res;

        VkXlibSurfaceCreateInfoKHR xlib_surface_create_info = {
                .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                .dpy = window->window_info.info.x11.display,
                .window = window->window_info.info.x11.window
        };
        res = fv_vk.vkCreateXlibSurfaceKHR(window->vk_instance,
                                           &xlib_surface_create_info,
                                           NULL, /* allocator */
                                           &window->vk_surface);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating Xlib Vulkan surface");
                return false;
        }

        return true;
}
#endif /* SDL_VIDEO_DRIVER_X11 */

#ifdef SDL_VIDEO_DRIVER_WAYLAND
static bool
create_vk_surface_wayland(struct fv_window *window)
{
        VkResult res;

        VkWaylandSurfaceCreateInfoKHR xlib_surface_create_info = {
                .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                .display = window->window_info.info.wl.display,
                .surface = window->window_info.info.wl.surface
        };
        res = fv_vk.vkCreateWaylandSurfaceKHR(window->vk_instance,
                                              &xlib_surface_create_info,
                                              NULL, /* allocator */
                                              &window->vk_surface);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating Wayland Vulkan surface");
                return false;
        }

        return true;
}
#endif /* SDL_VIDEO_DRIVER_WAYLAND */

#ifdef SDL_VIDEO_DRIVER_WINDOWS
static bool
create_vk_surface_windows(struct fv_window *window)
{
        VkResult res;

        VkWin32SurfaceCreateInfoKHR win32_surface_create_info = {
                .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                .hinstance = GetModuleHandle(NULL),
                .hwnd = window->window_info.info.win.window
        };
        res = fv_vk.vkCreateWin32SurfaceKHR(window->vk_instance,
                                            &win32_surface_create_info,
                                            NULL, /* allocator */
                                            &window->vk_surface);
        if (res != VK_SUCCESS) {
                fv_error_message("Error allocating Win32 Vulkan surface");
                return false;
        }

        return true;
}
#endif /* SDL_VIDEO_DRIVER_WINDOWS */

static bool
create_vk_surface(struct fv_window *window)
{
        switch (window->window_info.subsystem) {
#ifdef SDL_VIDEO_DRIVER_X11
        case SDL_SYSWM_X11:
                return create_vk_surface_x11(window);
#endif
#ifdef SDL_VIDEO_DRIVER_WAYLAND
        case SDL_SYSWM_WAYLAND:
                return create_vk_surface_wayland(window);
#endif
#ifdef SDL_VIDEO_DRIVER_WINDOWS
        case SDL_SYSWM_WINDOWS:
                return create_vk_surface_windows(window);
#endif
        default:
                fv_error_message("Unknown window system chosen by SDL");
                return false;
        }
}

static const char *
get_system_surface_extension(struct fv_window *window)
{
        switch (window->window_info.subsystem) {
#ifdef SDL_VIDEO_DRIVER_X11
        case SDL_SYSWM_X11:
                return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
#ifdef SDL_VIDEO_DRIVER_WAYLAND
        case SDL_SYSWM_WAYLAND:
                return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#endif
#ifdef SDL_VIDEO_DRIVER_WINDOWS
        case SDL_SYSWM_WINDOWS:
                return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif
        default:
                fv_error_message("Unknown window system chosen by SDL");
                return false;
        }
}

static bool
init_vk(struct fv_window *window)
{
        VkPhysicalDeviceMemoryProperties *memory_properties =
                &window->vk_data.memory_properties;
        VkResult res;
        const char *sys_surface_extension;
        struct fv_vk_data *vk_data = &window->vk_data;

        sys_surface_extension = get_system_surface_extension(window);
        if (sys_surface_extension == NULL)
                return false;

        struct VkInstanceCreateInfo instance_create_info = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &(VkApplicationInfo) {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .pApplicationName = "finvenkisto",
                        .apiVersion = VK_MAKE_VERSION(1, 0, 2)
                },
                .enabledExtensionCount = 2,
                .ppEnabledExtensionNames = (const char * const []) {
                        VK_KHR_SURFACE_EXTENSION_NAME,
                        sys_surface_extension
                },
        };
        res = fv_vk.vkCreateInstance(&instance_create_info,
                                     NULL, /* allocator */
                                     &window->vk_instance);

        if (res != VK_SUCCESS) {
                fv_error_message("Failed to create VkInstance");
                goto error;
        }

        fv_vk_init_instance(window->vk_instance);

        if (!create_vk_surface(window))
                goto error;

        if (!find_physical_device(window))
                goto error;

        fv_vk.vkGetPhysicalDeviceProperties(vk_data->physical_device,
                                            &vk_data->device_properties);
        fv_vk.vkGetPhysicalDeviceMemoryProperties(vk_data->physical_device,
                                                  memory_properties);

        window->vk_depth_format = get_depth_format(window);

        VkPhysicalDeviceFeatures features;
        memset(&features, 0, sizeof features);

        VkDeviceCreateInfo device_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .queueFamilyIndex = vk_data->queue_family,
                        .queueCount = 1,
                        .pQueuePriorities = (float[]) { 1.0f }
                },
                .pEnabledFeatures = &features,
                .enabledExtensionCount = 1,
                .ppEnabledExtensionNames = (const char * const []) {
                        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                },
        };
        res = fv_vk.vkCreateDevice(vk_data->physical_device,
                                   &device_create_info,
                                   NULL, /* allocator */
                                   &vk_data->device);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkDevice");
                goto error;
        }

        fv_vk_init_device(vk_data->device);

        fv_vk.vkGetDeviceQueue(vk_data->device,
                               vk_data->queue_family,
                               0, /* queueIndex */
                               &vk_data->queue);

        VkSemaphoreCreateInfo semaphore_create_info = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        res = fv_vk.vkCreateSemaphore(vk_data->device,
                                      &semaphore_create_info,
                                      NULL, /* allocator */
                                      &window->vk_semaphore);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating semaphore");
                goto error;
        }

        if (!find_surface_format(window))
                goto error;

        if (!find_present_mode(window))
                goto error;

        VkCommandPoolCreateInfo command_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = vk_data->queue_family
        };
        res = fv_vk.vkCreateCommandPool(vk_data->device,
                                        &command_pool_create_info,
                                        NULL, /* allocator */
                                        &vk_data->command_pool);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkCommandPool");
                goto error;
        }

        VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = vk_data->command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
        };
        res = fv_vk.vkAllocateCommandBuffers(vk_data->device,
                                             &command_buffer_allocate_info,
                                             &vk_data->command_buffer);

        if (res != VK_SUCCESS) {
                fv_error_message("Error creating command buffer");
                goto error;
        }

        VkDescriptorPoolSize pool_size = {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 4
        };
        VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets = 4,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size
        };
        res = fv_vk.vkCreateDescriptorPool(vk_data->device,
                                           &descriptor_pool_create_info,
                                           NULL, /* allocator */
                                           &vk_data->descriptor_pool);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating VkDescriptorPool");
                goto error;
        }

        VkAttachmentDescription attachment_descriptions[] = {
                {
                        .format = window->vk_surface_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                },
                {
                        .format = window->vk_depth_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout =
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                },
        };
        VkSubpassDescription subpass_descriptions[] = {
                {
                        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &(VkAttachmentReference) {
                                .attachment = 0,
                                .layout =
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                        },
                        .pDepthStencilAttachment = &(VkAttachmentReference) {
                                .attachment = 1,
                                .layout =
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                        }
                }
        };
        VkRenderPassCreateInfo render_pass_create_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = FV_N_ELEMENTS(attachment_descriptions),
                .pAttachments = attachment_descriptions,
                .subpassCount = FV_N_ELEMENTS(subpass_descriptions),
                .pSubpasses = subpass_descriptions
        };
        res = fv_vk.vkCreateRenderPass(vk_data->device,
                                       &render_pass_create_info,
                                       NULL, /* allocator */
                                       &vk_data->render_pass);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating render pass");
                goto error;
        }

        VkFenceCreateInfo fence_create_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        res = fv_vk.vkCreateFence(vk_data->device,
                                  &fence_create_info,
                                  NULL, /* allocator */
                                  &window->vk_fence);
        if (res != VK_SUCCESS) {
                fv_error_message("Error creating fence");
                goto error;
        }

        return true;

error:
        deinit_vk(window);
        return false;
}

struct fv_window *
fv_window_new(bool is_fullscreen)
{
        struct fv_window *window = fv_calloc(sizeof *window);
        Uint32 flags;
        int res;

        window->is_fullscreen = is_fullscreen;

        if (!fv_vk_load_libvulkan())
                goto error;

        window->libvulkan_loaded = true;

        res = SDL_Init(SDL_INIT_VIDEO |
                       SDL_INIT_JOYSTICK |
                       SDL_INIT_GAMECONTROLLER);
        if (res < 0) {
                fv_error_message("Unable to init SDL: %s\n", SDL_GetError());
                goto error;
        }

        window->sdl_inited = true;

        flags = SDL_WINDOW_RESIZABLE;
        if (window->is_fullscreen)
                flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

        window->window = SDL_CreateWindow("Finvenkisto",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          800, 600,
                                          flags);
        if (window->window == NULL) {
                fv_error_message("Failed to create SDL window: %s",
                                 SDL_GetError());
                goto error;
        }

        SDL_VERSION(&window->window_info.version);

        if (!SDL_GetWindowWMInfo(window->window, &window->window_info)) {
                fv_error_message("Error getting SDL window info: %s",
                                 SDL_GetError());
                goto error;
        }

        if (!init_vk(window))
                goto error;

        return window;

error:
        fv_window_free(window);
        return NULL;
}

void
fv_window_get_extent(struct fv_window *window,
                     VkExtent2D *extent)
{
        *extent = window->vk_fb.extent;
}

struct fv_vk_data *
fv_window_get_vk_data(struct fv_window *window)
{
        return &window->vk_data;
}

void
fv_window_free(struct fv_window *window)
{
        destroy_framebuffer_resources(window);

        deinit_vk(window);

        if (window->window)
                SDL_DestroyWindow(window->window);

        if (window->sdl_inited)
                SDL_Quit();

        if (window->libvulkan_loaded)
                fv_vk_unload_libvulkan();

        fv_free(window);
}
