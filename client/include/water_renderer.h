#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include "water_sim.h"
#include "chunk.h"

struct WaterGpuChunk {
    VkBuffer      vertBuf   = VK_NULL_HANDLE;
    VmaAllocation vertAlloc = nullptr;
    VkBuffer      idxBuf    = VK_NULL_HANDLE;
    VmaAllocation idxAlloc  = nullptr;
    uint32_t      idxCount  = 0;
};

class WaterRenderer {
public:
    float waterTime = 0.f; // advance each frame

    void init(VkDevice device, VmaAllocator allocator,
              VkCommandPool pool, VkQueue queue,
              VkRenderPass renderPass, VkExtent2D extent,
              const char* vertSpv, const char* fragSpv);

    void destroy(VkDevice device, VmaAllocator allocator);

    // Upload or update a water chunk mesh
    void uploadChunk(VkDevice device, VmaAllocator allocator,
                     VkCommandPool pool, VkQueue queue,
                     const WaterMesh& mesh);

    void removeChunk(VkDevice device, VmaAllocator allocator, ChunkCoord coord);

    void update(float dt) { waterTime += dt; }

    void draw(VkCommandBuffer cmd, const glm::mat4& viewProj,
              VkExtent2D extent, glm::vec3 camPos,
              float sunIntensity) const;

private:
    VkDevice     _device    = VK_NULL_HANDLE;
    VmaAllocator _allocator = nullptr;
    VkCommandPool _pool     = VK_NULL_HANDLE;
    VkQueue       _queue    = VK_NULL_HANDLE;

    VkPipeline       _pipeline = VK_NULL_HANDLE;
    VkPipelineLayout _layout   = VK_NULL_HANDLE;

    std::unordered_map<ChunkCoord, WaterGpuChunk, ChunkCoordHash> _chunks;

    static VkBuffer uploadBuf(VkDevice dev, VmaAllocator alloc,
                               VkCommandPool pool, VkQueue q,
                               const void* data, VkDeviceSize size,
                               VkBufferUsageFlags usage,
                               VmaAllocation& outAlloc);
};
