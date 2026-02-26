#pragma once
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <deque>
#include "chunk.h"

// Forward declare to avoid circular include
struct ViewModelRenderer;

// ── GpuChunk ──────────────────────────────────────────────────────────────────
struct GpuChunk {
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t vertexCount;
};

// ── Upload queued from game thread ────────────────────────────────────────────
struct PendingUpload {
    ChunkCoord            coord;
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

static constexpr uint32_t MEGA_VERTEX_CAP = 1 << 21;
static constexpr uint32_t MEGA_INDEX_CAP  = 1 << 21;

// ── MegaBuffer ────────────────────────────────────────────────────────────────
struct MegaBuffer {
    VkBuffer      vertexBuffer = VK_NULL_HANDLE;
    VkBuffer      indexBuffer  = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc  = nullptr;
    VmaAllocation indexAlloc   = nullptr;

    struct Range { uint32_t offset, size; };
    std::vector<Range> vertRanges;
    std::vector<Range> indRanges;

    uint32_t allocVerts  (uint32_t count);
    uint32_t allocInds   (uint32_t count);
    void     releaseVerts(uint32_t offset, uint32_t count);
    void     releaseInds (uint32_t offset, uint32_t count);
};

struct DrawCmd {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

struct ChunkDrawData {
    glm::mat4 model;
    glm::vec4 params;
};

// ── VkContext ─────────────────────────────────────────────────────────────────
struct VkContext {
    vkb::Instance  instance;
    vkb::Device    device;
    vkb::Swapchain swapchain;

    VkImage       depthImage     = VK_NULL_HANDLE;
    VkImageView   depthImageView = VK_NULL_HANDLE;
    VmaAllocation depthAlloc     = nullptr;

    VkBuffer      stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc  = nullptr;
    void*         stagingMapped = nullptr;
    VkDeviceSize  stagingSize   = 64 * 1024 * 1024;

    VkCommandBuffer uploadCmd   = VK_NULL_HANDLE;
    VkFence         uploadFence = VK_NULL_HANDLE;

    MegaBuffer mega;

    static constexpr uint32_t MAX_DRAW_CHUNKS = 512;

    VkBuffer      indirectBuffer[2] = {};
    VmaAllocation indirectAlloc[2]  = {};
    void*         indirectMapped[2] = {};

    VkBuffer      perChunkBuffer[2] = {};
    VmaAllocation perChunkAlloc[2]  = {};
    void*         perChunkMapped[2] = {};

    VkSurfaceKHR surface             = VK_NULL_HANDLE;
    VkQueue      graphicsQueue       = VK_NULL_HANDLE;
    uint32_t     graphicsQueueFamily = 0;

    VkCommandPool                commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkImage>       swapImages;
    std::vector<VkImageView>   swapImageViews;
    std::vector<VkFramebuffer> framebuffers;

    VkRenderPass          renderPass     = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsLayout       = VK_NULL_HANDLE;
    VkDescriptorPool      dsPool         = VK_NULL_HANDLE;
    VkDescriptorSet       dsSets[2]      = {};
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            pipeline       = VK_NULL_HANDLE;

    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence>     inFlight;

    VmaAllocator allocator = nullptr;

    std::unordered_map<ChunkCoord, GpuChunk, ChunkCoordHash> chunks;
    std::deque<PendingUpload> uploadQueue;

    static constexpr int FRAMES_IN_FLIGHT = 2;
    uint32_t currentFrame = 0;
};

VkContext vk_init(GLFWwindow* window);
void      vk_destroy(VkContext& ctx);

// viewModel may be nullptr — skips viewmodel draw
void      vk_draw(VkContext& ctx, const glm::mat4& viewProj,
                  float sunIntensity, glm::vec3 skyColor,
                  const ViewModelRenderer* viewModel = nullptr,
                  const glm::mat4& proj = glm::mat4(1.f));

void      vk_upload_chunk(VkContext& ctx, const ChunkMesh& mesh);
void      vk_remove_chunk(VkContext& ctx, ChunkCoord coord);
