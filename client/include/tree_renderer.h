#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include "chunk.h"

// ── Vertex formats ────────────────────────────────────────────────────────────

struct TreeVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    float     noiseVal;
};

struct LeafVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    float     noiseVal;
    float     flutter;
};

struct TreeRenderInstance {
    glm::vec3 pos;
    float     yaw;
    float     scale;
    float     seed;
};

// ── Procedural mesh ───────────────────────────────────────────────────────────

struct TreeMeshData {
    std::vector<TreeVertex>  trunkVerts;
    std::vector<uint32_t>    trunkIndices;
    std::vector<LeafVertex>  leafVerts;
    std::vector<uint32_t>    leafIndices;
};

TreeMeshData buildTreeMesh(int templateIdx, int sides,
                            float trunkHeight,
                            float trunkRadiusBase, float trunkRadiusTip,
                            int branchCount, float canopyRadius,
                            uint32_t seed);

// ── GPU mesh ──────────────────────────────────────────────────────────────────

struct TreeGpuMesh {
    VkBuffer      trunkVBuf   = VK_NULL_HANDLE;
    VmaAllocation trunkVAlloc = nullptr;
    VkBuffer      trunkIBuf   = VK_NULL_HANDLE;
    VmaAllocation trunkIAlloc = nullptr;
    uint32_t      trunkIdxCount = 0;

    VkBuffer      leafVBuf    = VK_NULL_HANDLE;
    VmaAllocation leafVAlloc  = nullptr;
    VkBuffer      leafIBuf    = VK_NULL_HANDLE;
    VmaAllocation leafIAlloc  = nullptr;
    uint32_t      leafIdxCount = 0;
};

// ── Renderer ──────────────────────────────────────────────────────────────────

static constexpr int TREE_TEMPLATE_COUNT = 6;

class TreeRenderer {
public:
    float windTime = 0.f;

    void init(VkDevice device, VmaAllocator allocator,
              VkCommandPool pool, VkQueue queue,
              VkRenderPass renderPass, VkExtent2D extent,
              const char* trunkVertSpv, const char* trunkFragSpv,
              const char* leafVertSpv,  const char* leafFragSpv);

    void destroy(VkDevice device, VmaAllocator allocator);

    void addTree(glm::vec3 pos, float yaw, float scale, int templateIdx);
    void clearTrees();
    void removeTreesInChunk(int chunkX, int chunkZ);

    void update(float dt) { windTime += dt; }

    void draw(VkCommandBuffer cmd, const glm::mat4& viewProj,
              VkExtent2D extent) const;

private:
    VkDevice     _device    = VK_NULL_HANDLE;
    VmaAllocator _allocator = nullptr;
VkCommandPool _pool  = VK_NULL_HANDLE;
VkQueue       _queue = VK_NULL_HANDLE;
    std::array<TreeGpuMesh,                       TREE_TEMPLATE_COUNT> _meshes{};
    std::array<std::vector<TreeRenderInstance>,          TREE_TEMPLATE_COUNT> _instances;
    std::array<VkBuffer,                           TREE_TEMPLATE_COUNT> _instBuf{};
    std::array<VmaAllocation,                      TREE_TEMPLATE_COUNT> _instAlloc{};
    std::array<uint32_t,                           TREE_TEMPLATE_COUNT> _instCount{};
    bool _instDirty = true;

    VkPipeline       _trunkPipeline = VK_NULL_HANDLE;
    VkPipelineLayout _trunkLayout   = VK_NULL_HANDLE;
    VkPipeline       _leafPipeline  = VK_NULL_HANDLE;
    VkPipelineLayout _leafLayout    = VK_NULL_HANDLE;

    void uploadInstances(VkCommandPool pool, VkQueue queue);
    void uploadMesh(VkCommandPool pool, VkQueue queue,
                    int idx, const TreeMeshData& data);
    void createPipelines(VkDevice device,
                         VkRenderPass rp, VkExtent2D ext,
                         const char* tvs, const char* tfs,
                         const char* lvs, const char* lfs);

    static VkBuffer uploadBuf(VkDevice dev, VmaAllocator alloc,
                              VkCommandPool pool, VkQueue q,
                              const void* data, VkDeviceSize size,
                              VkBufferUsageFlags usage,
                              VmaAllocation& outAlloc);
};
