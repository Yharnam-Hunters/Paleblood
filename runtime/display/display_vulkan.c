/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The display's window back end: an SDL3 window, shown with Vulkan.

   Each presented frame (host memory, BGRA) is copied into a staging buffer, from there into a
   frame image of the frame's own size, and then scaled into the window's swapchain image with a
   filtered blit, so the window can be any size. One frame is in flight at a time: showing waits
   until the previous copy has finished. The swapchain is rebuilt when the window changes size.

   Pacing belongs to video out, which already waits for its 60 Hz tick before a vsync flip, so the
   window presents without waiting for its own vblank when the device allows it (mailbox, then
   immediate; FIFO otherwise). Closing the window ends the process, as closing a game does. */
#include "runtime/display_window.h"
#include "runtime/process.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

enum { MAX_SWAPCHAIN_IMAGES = 8 };
#define BYTES_PER_PIXEL 4
#define FRAME_FORMAT VK_FORMAT_B8G8R8A8_UNORM

static struct {
    SDL_Window *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkExtent2D swapchain_extent;
    uint32_t image_count;
    VkImage swapchain_images[MAX_SWAPCHAIN_IMAGES];
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    void *staging_pixels;
    VkImage frame;
    VkDeviceMemory frame_memory;
    uint32_t width, height;
    VkCommandPool pool;
    VkCommandBuffer commands;
    VkSemaphore image_ready, copy_done;
    VkFence in_flight;
} vk;

static void check(VkResult result, const char *what)
{
    if (result != VK_SUCCESS) rt_fatal("display window", what);
}

int rt_display_window_available(void) { return 1; }

static uint32_t memory_type(uint32_t allowed, VkMemoryPropertyFlags wanted)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
        if ((allowed & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    rt_fatal("display window", "the device has no memory of the kind the display needs");
}

static void create_instance(void)
{
    Uint32 extension_count = 0;
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (!extensions) rt_fatal("display window", "SDL names no Vulkan instance extensions");
    const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                   .pApplicationName = "Paleblood",
                                   .apiVersion = VK_API_VERSION_1_1};
    const VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                       .pApplicationInfo = &app,
                                       .enabledExtensionCount = extension_count,
                                       .ppEnabledExtensionNames = extensions};
    check(vkCreateInstance(&info, NULL, &vk.instance), "Vulkan refused to create an instance");
    if (!SDL_Vulkan_CreateSurface(vk.window, vk.instance, NULL, &vk.surface))
        rt_fatal("display window", "SDL could not create a Vulkan surface for the window");
}

/* The first device with a queue that can both copy and present to the window. */
static void choose_device(void)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(vk.instance, &count, NULL);
    VkPhysicalDevice devices[16];
    if (count > 16) count = 16;
    vkEnumeratePhysicalDevices(vk.instance, &count, devices);
    for (uint32_t d = 0; d < count; d++) {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &families, NULL);
        VkQueueFamilyProperties properties[32];
        if (families > 32) families = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &families, properties);
        for (uint32_t f = 0; f < families; f++) {
            VkBool32 presents = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], f, vk.surface, &presents);
            if (presents && (properties[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                vk.physical = devices[d];
                vk.queue_family = f;
                return;
            }
        }
    }
    rt_fatal("display window", "no Vulkan device can present to the window");
}

static void create_device(void)
{
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                           .queueFamilyIndex = vk.queue_family,
                                           .queueCount = 1,
                                           .pQueuePriorities = &priority};
    const char *const extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                     .queueCreateInfoCount = 1,
                                     .pQueueCreateInfos = &queue,
                                     .enabledExtensionCount = 1,
                                     .ppEnabledExtensionNames = extensions};
    check(vkCreateDevice(vk.physical, &info, NULL, &vk.device), "Vulkan refused to create a device");
    vkGetDeviceQueue(vk.device, vk.queue_family, 0, &vk.queue);
}

/* Mailbox, then immediate: video out paces the frames already. FIFO is always there. */
static VkPresentModeKHR present_mode(void)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, NULL);
    VkPresentModeKHR modes[16];
    if (count > 16) count = 16;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, modes);
    static const VkPresentModeKHR preferred[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
    for (size_t p = 0; p < sizeof preferred / sizeof preferred[0]; p++)
        for (uint32_t i = 0; i < count; i++)
            if (modes[i] == preferred[p]) return preferred[p];
    return VK_PRESENT_MODE_FIFO_KHR;
}

static void create_swapchain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &caps), "the window's surface has no capabilities");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        rt_fatal("display window", "the window's images cannot be copied into");
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(vk.window, &w, &h);
        extent = (VkExtent2D){(uint32_t)w, (uint32_t)h};
    }
    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount && images > caps.maxImageCount) images = caps.maxImageCount;
    const VkSwapchainKHR old = vk.swapchain;
    const VkSwapchainCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                           .surface = vk.surface,
                                           .minImageCount = images,
                                           .imageFormat = FRAME_FORMAT,
                                           .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                                           .imageExtent = extent,
                                           .imageArrayLayers = 1,
                                           .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                           .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                           .preTransform = caps.currentTransform,
                                           .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                           .presentMode = present_mode(),
                                           .clipped = VK_TRUE,
                                           .oldSwapchain = old};
    check(vkCreateSwapchainKHR(vk.device, &info, NULL, &vk.swapchain), "Vulkan refused to create the window's swapchain");
    if (old) vkDestroySwapchainKHR(vk.device, old, NULL);
    vk.swapchain_extent = extent;
    vk.image_count = MAX_SWAPCHAIN_IMAGES;
    check(vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.image_count, vk.swapchain_images),
          "the window's swapchain has more images than the display holds");
}

static void create_frame_resources(uint32_t width, uint32_t height)
{
    const VkDeviceSize bytes = (VkDeviceSize)width * height * BYTES_PER_PIXEL;
    const VkBufferCreateInfo buffer = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                       .size = bytes,
                                       .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    check(vkCreateBuffer(vk.device, &buffer, NULL, &vk.staging), "Vulkan refused the staging buffer");
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(vk.device, vk.staging, &need);
    const VkMemoryAllocateInfo staging_memory = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = need.size,
        .memoryTypeIndex = memory_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    check(vkAllocateMemory(vk.device, &staging_memory, NULL, &vk.staging_memory), "no memory for the staging buffer");
    check(vkBindBufferMemory(vk.device, vk.staging, vk.staging_memory, 0), "the staging buffer could not be bound");
    check(vkMapMemory(vk.device, vk.staging_memory, 0, bytes, 0, &vk.staging_pixels), "the staging buffer could not be mapped");

    const VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                     .imageType = VK_IMAGE_TYPE_2D,
                                     .format = FRAME_FORMAT,
                                     .extent = {width, height, 1},
                                     .mipLevels = 1,
                                     .arrayLayers = 1,
                                     .samples = VK_SAMPLE_COUNT_1_BIT,
                                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                     .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    check(vkCreateImage(vk.device, &image, NULL, &vk.frame), "Vulkan refused the frame image");
    vkGetImageMemoryRequirements(vk.device, vk.frame, &need);
    const VkMemoryAllocateInfo frame_memory = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                               .allocationSize = need.size,
                                               .memoryTypeIndex = memory_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    check(vkAllocateMemory(vk.device, &frame_memory, NULL, &vk.frame_memory), "no memory for the frame image");
    check(vkBindImageMemory(vk.device, vk.frame, vk.frame_memory, 0), "the frame image could not be bound");
    vk.width = width, vk.height = height;

    const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                          .queueFamilyIndex = vk.queue_family};
    check(vkCreateCommandPool(vk.device, &pool, NULL, &vk.pool), "Vulkan refused a command pool");
    const VkCommandBufferAllocateInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                  .commandPool = vk.pool,
                                                  .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                  .commandBufferCount = 1};
    check(vkAllocateCommandBuffers(vk.device, &commands, &vk.commands), "Vulkan refused a command buffer");
    const VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(vk.device, &semaphore, NULL, &vk.image_ready), "Vulkan refused a semaphore");
    check(vkCreateSemaphore(vk.device, &semaphore, NULL, &vk.copy_done), "Vulkan refused a semaphore");
    const VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    check(vkCreateFence(vk.device, &fence, NULL, &vk.in_flight), "Vulkan refused a fence");
}

int rt_display_window_open(uint32_t width, uint32_t height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return -1;
    vk.window = SDL_CreateWindow("Paleblood", (int)width, (int)height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!vk.window) return -1;
    create_instance();
    choose_device();
    create_device();
    create_swapchain();
    create_frame_resources(width, height);
    return 0;
}

static void barrier(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst)
{
    const VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = src,
                                    .dstAccessMask = dst,
                                    .oldLayout = from,
                                    .newLayout = to,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = image,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(vk.commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

/* Staging buffer to frame image, frame image scaled into the swapchain image. */
static void record_copy(VkImage target)
{
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    check(vkBeginCommandBuffer(vk.commands, &begin), "a command buffer could not begin");
    barrier(vk.frame, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkBufferImageCopy upload = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                      .imageExtent = {vk.width, vk.height, 1}};
    vkCmdCopyBufferToImage(vk.commands, vk.staging, vk.frame, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload);
    barrier(vk.frame, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    barrier(target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkImageBlit scale = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                               .srcOffsets = {{0, 0, 0}, {(int32_t)vk.width, (int32_t)vk.height, 1}},
                               .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                               .dstOffsets = {{0, 0, 0}, {(int32_t)vk.swapchain_extent.width, (int32_t)vk.swapchain_extent.height, 1}}};
    vkCmdBlitImage(vk.commands, vk.frame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &scale, VK_FILTER_LINEAR);
    barrier(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    check(vkEndCommandBuffer(vk.commands), "a command buffer could not end");
}

/* Window events: closing the window ends the process; a size change rebuilds the swapchain. */
static int handle_events(void)
{
    int resized = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) exit(0);
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) resized = 1;
    }
    return resized;
}

static void rebuild_swapchain(void)
{
    vkDeviceWaitIdle(vk.device);
    create_swapchain();
}

void rt_display_window_show(const uint32_t *pixels, uint32_t width, uint32_t height)
{
    if (width != vk.width || height != vk.height) rt_fatal("display window", "a frame of a size the window was not opened for");
    if (handle_events()) rebuild_swapchain();
    if (!vk.swapchain_extent.width || !vk.swapchain_extent.height) return; /* minimised: nothing to show */
    check(vkWaitForFences(vk.device, 1, &vk.in_flight, VK_TRUE, UINT64_MAX), "waiting for the last frame failed");
    uint32_t index;
    VkResult acquired = vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.image_ready, VK_NULL_HANDLE, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuild_swapchain();
        return; /* this frame is skipped; the next one goes to the new swapchain */
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) rt_fatal("display window", "no window image to show the frame in");
    check(vkResetFences(vk.device, 1, &vk.in_flight), "the frame fence could not be reset");
    memcpy(vk.staging_pixels, pixels, (size_t)width * height * BYTES_PER_PIXEL);
    record_copy(vk.swapchain_images[index]);
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .waitSemaphoreCount = 1,
                                 .pWaitSemaphores = &vk.image_ready,
                                 .pWaitDstStageMask = &wait_stage,
                                 .commandBufferCount = 1,
                                 .pCommandBuffers = &vk.commands,
                                 .signalSemaphoreCount = 1,
                                 .pSignalSemaphores = &vk.copy_done};
    check(vkQueueSubmit(vk.queue, 1, &submit, vk.in_flight), "the frame copy could not be submitted");
    const VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                      .waitSemaphoreCount = 1,
                                      .pWaitSemaphores = &vk.copy_done,
                                      .swapchainCount = 1,
                                      .pSwapchains = &vk.swapchain,
                                      .pImageIndices = &index};
    const VkResult shown = vkQueuePresentKHR(vk.queue, &present);
    if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR) rebuild_swapchain();
    else if (shown != VK_SUCCESS) rt_fatal("display window", "the window could not show the frame");
}
