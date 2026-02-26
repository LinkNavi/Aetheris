#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include "gltf_loader.h"

// GPU buffers for one loaded GLB mesh
struct ViewModelMesh {
    VkBuffer      vertBuf   = VK_NULL_HANDLE;
    VmaAllocation vertAlloc = nullptr;
    VkBuffer      idxBuf    = VK_NULL_HANDLE;
    VmaAllocation idxAlloc  = nullptr;
    uint32_t      indexCount = 0;
};

// Position/rotation/scale of the weapon in view space.
// Tweak per weapon to sit correctly in the player's hand.
struct ViewModelTransform {
    glm::vec3 offset   = { 0.25f, -0.28f, -0.45f }; // right, down, forward
    glm::vec3 rotation = { 0.f,   0.f,    0.f };     // degrees XYZ
    glm::vec3 scale    = { 1.f,   1.f,    1.f };
};

// Owns the viewmodel pipeline and all loaded meshes.
// One instance lives in VkContext (or main).
struct ViewModelRenderer {
    // Pipeline objects
    VkPipeline            pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;

    // Loaded meshes — index returned by loadMesh()
    std::vector<ViewModelMesh> meshes;

    // Which mesh is currently equipped (-1 = nothing / fists)
    int activeMesh = -1;

    // Per-weapon hand transforms
    std::vector<ViewModelTransform> transforms;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void init(VkDevice device, VmaAllocator allocator,
              VkRenderPass renderPass, VkExtent2D extent,
              const char* vertSpv, const char* fragSpv);

    void destroy(VkDevice device, VmaAllocator allocator);

    // Upload a GLB to GPU. Returns mesh index or -1 on failure.
    int loadMesh(VkDevice device, VmaAllocator allocator,
                 VkCommandPool pool, VkQueue queue,
                 const GltfModel& model,
                 ViewModelTransform transform = {});

    // Record draw commands. Call after terrain draw, still inside render pass.
    // proj: same projection matrix used for the scene.
    void draw(VkCommandBuffer cmd, const glm::mat4& proj) const;
};
