#pragma once
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include "chunk.h"



struct GpuChunk {
    VkBuffer      vertexBuffer;
    VkBuffer      indexBuffer;
    VmaAllocation vertexAlloc;
    VmaAllocation indexAlloc;
    uint32_t      indexCount;
};

struct VkContext {
    vkb::Instance    instance;
    vkb::Device      device;
    vkb::Swapchain   swapchain;
VkImage       depthImage;
VkImageView   depthImageView;
VmaAllocation depthAlloc;
VkBuffer      stagingBuffer = VK_NULL_HANDLE;
VmaAllocation stagingAlloc  = nullptr;
void*         stagingMapped = nullptr;
VkDeviceSize  stagingSize   = 32 * 1024 * 1024; // 32 MB
    VkSurfaceKHR     surface;
    VkQueue          graphicsQueue;
    uint32_t         graphicsQueueFamily;

    VkCommandPool                commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkImage>        swapImages;
    std::vector<VkImageView>    swapImageViews;
    std::vector<VkFramebuffer>  framebuffers;

    VkRenderPass     renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline       pipeline;

    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence>     inFlight;

    VmaAllocator allocator;

    std::unordered_map<ChunkCoord, GpuChunk, ChunkCoordHash> chunks;

    static constexpr int FRAMES_IN_FLIGHT = 2;
    uint32_t currentFrame = 0;
};

VkContext vk_init(GLFWwindow* window);
void      vk_destroy(VkContext& ctx);
void      vk_draw(VkContext& ctx, const glm::mat4& viewProj); // <-- takes VP from caller
void      vk_upload_chunk(VkContext& ctx, const ChunkMesh& mesh);
