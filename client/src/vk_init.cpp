#include "vk_context.h"
#include <stdexcept>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

static void check(VkResult r, const char* msg) {
    if (r != VK_SUCCESS) throw std::runtime_error(msg);
}

VkContext vk_init(GLFWwindow* window) {
    VkContext ctx;

    // ── Instance ─────────────────────────────────────────────────────────────
    auto inst = vkb::InstanceBuilder{}
        .set_app_name("Aetheris")
        .request_validation_layers(true)
        .require_api_version(1, 3, 0)
        .use_default_debug_messenger()
        .build();
    if (!inst) throw std::runtime_error(inst.error().message());
    ctx.instance = inst.value();

    // ── Surface ───────────────────────────────────────────────────────────────
    check(glfwCreateWindowSurface(ctx.instance.instance, window, nullptr, &ctx.surface),
          "Failed to create window surface");

    // ── Physical + Logical Device ─────────────────────────────────────────────
    auto phys = vkb::PhysicalDeviceSelector{ctx.instance}
        .set_surface(ctx.surface)
        .set_minimum_version(1, 3)
        .require_dedicated_transfer_queue()
        .select();
    if (!phys) throw std::runtime_error(phys.error().message());

    auto dev = vkb::DeviceBuilder{phys.value()}.build();
    if (!dev) throw std::runtime_error(dev.error().message());
    ctx.device = dev.value();

    auto gq = ctx.device.get_queue(vkb::QueueType::graphics);
    if (!gq) throw std::runtime_error("No graphics queue");
    ctx.graphicsQueue = gq.value();
    ctx.graphicsQueueFamily = ctx.device.get_queue_index(vkb::QueueType::graphics).value();

    // ── Swapchain ─────────────────────────────────────────────────────────────
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);

    auto sc = vkb::SwapchainBuilder{ctx.device}
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(w, h)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();
    if (!sc) throw std::runtime_error(sc.error().message());
    ctx.swapchain      = sc.value();
    ctx.swapImages     = ctx.swapchain.get_images().value();
    ctx.swapImageViews = ctx.swapchain.get_image_views().value();

    // ── Command Pool + Buffers ────────────────────────────────────────────────
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.graphicsQueueFamily;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(ctx.device.device, &poolInfo, nullptr, &ctx.commandPool),
          "Failed to create command pool");

    ctx.commandBuffers.resize(VkContext::FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = ctx.commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = VkContext::FRAMES_IN_FLIGHT;
    check(vkAllocateCommandBuffers(ctx.device.device, &allocInfo, ctx.commandBuffers.data()),
          "Failed to allocate command buffers");

    // ── Sync Objects ──────────────────────────────────────────────────────────
    ctx.imageAvailable.resize(VkContext::FRAMES_IN_FLIGHT);
    ctx.renderFinished.resize(VkContext::FRAMES_IN_FLIGHT);
    ctx.inFlight.resize(VkContext::FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < VkContext::FRAMES_IN_FLIGHT; i++) {
        check(vkCreateSemaphore(ctx.device.device, &semInfo, nullptr, &ctx.imageAvailable[i]), "semaphore");
        check(vkCreateSemaphore(ctx.device.device, &semInfo, nullptr, &ctx.renderFinished[i]), "semaphore");
        check(vkCreateFence(ctx.device.device, &fenceInfo, nullptr, &ctx.inFlight[i]), "fence");
    }

    // ── VMA ───────────────────────────────────────────────────────────────────
    VmaAllocatorCreateInfo vmaInfo{};
    vmaInfo.instance       = ctx.instance.instance;
    vmaInfo.physicalDevice = ctx.device.physical_device.physical_device;
    vmaInfo.device         = ctx.device.device;
    vmaInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    check(vmaCreateAllocator(&vmaInfo, &ctx.allocator), "Failed to create VMA allocator");

    return ctx;
}

void vk_destroy(VkContext& ctx) {
    vkDeviceWaitIdle(ctx.device.device);

    vmaDestroyAllocator(ctx.allocator);

    for (int i = 0; i < VkContext::FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(ctx.device.device, ctx.imageAvailable[i], nullptr);
        vkDestroySemaphore(ctx.device.device, ctx.renderFinished[i], nullptr);
        vkDestroyFence(ctx.device.device, ctx.inFlight[i], nullptr);
    }

    vkDestroyCommandPool(ctx.device.device, ctx.commandPool, nullptr);

    for (auto iv : ctx.swapImageViews)
        vkDestroyImageView(ctx.device.device, iv, nullptr);
    ctx.swapchain.destroy_image_views(ctx.swapImageViews);

    vkb::destroy_swapchain(ctx.swapchain);
    vkb::destroy_device(ctx.device);
    vkDestroySurfaceKHR(ctx.instance.instance, ctx.surface, nullptr);
    vkb::destroy_instance(ctx.instance);
}
