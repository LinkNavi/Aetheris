#pragma once
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <unordered_map>
#include "chunk.h"

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h = 0;
        h ^= std::hash<int>{}(c.x) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(c.y) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(c.z) + 0x9e3779b9 + (h<<6) + (h>>2);
        return h;
    }
};

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
void      vk_draw(VkContext& ctx);
void      vk_upload_chunk(VkContext& ctx, const ChunkMesh& mesh);
