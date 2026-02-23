#pragma once
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <vector>

struct VkContext {
    vkb::Instance       instance;
    vkb::Device         device;
    vkb::Swapchain      swapchain;

    VkSurfaceKHR        surface;
    VkQueue             graphicsQueue;
    uint32_t            graphicsQueueFamily;

    VkCommandPool       commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    // swapchain images
    std::vector<VkImage>     swapImages;
    std::vector<VkImageView> swapImageViews;

    // sync
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence>     inFlight;

    VmaAllocator allocator;

    static constexpr int FRAMES_IN_FLIGHT = 2;
    uint32_t currentFrame = 0;
};

// initializes everything, throws on failure
VkContext vk_init(GLFWwindow* window);
void      vk_destroy(VkContext& ctx);
