#pragma once
#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
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
    VkBuffer     vertexBuffer;
    VkBuffer     indexBuffer;
    VmaAllocation vertexAlloc;
    VmaAllocation indexAlloc;
    uint32_t     indexCount;
};

class Renderer {
public:
    explicit Renderer(SDL_Window* window);
    ~Renderer();

    void uploadChunk(ChunkMesh mesh);
    void draw();

private:
    void initVulkan(SDL_Window* window);
    void initSwapchain();
    void initRenderPass();
    void initPipeline();
    void initFramebuffers();
    void initCommands();
    void initSync();

    vkb::Instance       _instance;
    vkb::Device         _device;
    vkb::Swapchain      _swapchain;
    VkSurfaceKHR        _surface;
    VkQueue             _graphicsQueue;
    VkRenderPass        _renderPass;
    VkPipeline          _pipeline;
    VkPipelineLayout    _pipelineLayout;
    VmaAllocator        _allocator;

    std::unordered_map<ChunkCoord, GpuChunk, ChunkCoordHash> _chunks;
};
