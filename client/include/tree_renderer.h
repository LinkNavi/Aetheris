#pragma once
#include "chunk.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
// ── Vertex formats
// ────────────────────────────────────────────────────────────

struct TreeVertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
  float noiseVal;
};

struct LeafVertex {
  glm::vec3 pos;
  glm::vec2 uv;
  float noiseVal;
  float flutter;
};

struct TreeRenderInstance {
  glm::vec3 pos;
  float yaw;
  float scale;
  float seed;
};
struct FallingTree {
  glm::vec3 pos;
  float yaw;
  float scale;
  int templateIdx;
  float fallAngle = 0.f;
  float fallDir = 0.f;
  float age = 0.f;
  bool done = false;
};
// ── Procedural mesh
// ───────────────────────────────────────────────────────────

struct TreeMeshData {
  std::vector<TreeVertex> trunkVerts;
  std::vector<uint32_t> trunkIndices;
  std::vector<LeafVertex> leafVerts;
  std::vector<uint32_t> leafIndices;
};

TreeMeshData buildTreeMesh(int templateIdx, int sides, float trunkHeight,
                           float trunkRadiusBase, float trunkRadiusTip,
                           int branchCount, float canopyRadius, uint32_t seed,
                           float leafDensity);

// ── GPU mesh
// ──────────────────────────────────────────────────────────────────

struct TreeGpuMesh {
  VkBuffer trunkVBuf = VK_NULL_HANDLE;
  VmaAllocation trunkVAlloc = nullptr;
  VkBuffer trunkIBuf = VK_NULL_HANDLE;
  VmaAllocation trunkIAlloc = nullptr;
  uint32_t trunkIdxCount = 0;

  VkBuffer leafVBuf = VK_NULL_HANDLE;
  VmaAllocation leafVAlloc = nullptr;
  VkBuffer leafIBuf = VK_NULL_HANDLE;
  VmaAllocation leafIAlloc = nullptr;
  uint32_t leafIdxCount = 0;
};

// ── Renderer
// ──────────────────────────────────────────────────────────────────

static constexpr int TREE_TEMPLATE_COUNT = 6;

class TreeRenderer {
public:
  float windTime = 0.f;
  float leafDensity = 1.0f;
  void init(VkDevice device, VmaAllocator allocator, VkCommandPool pool,
            VkQueue queue, VkRenderPass renderPass, VkExtent2D extent,
            const char *trunkVertSpv, const char *trunkFragSpv,
            const char *leafVertSpv, const char *leafFragSpv);

  void destroy(VkDevice device, VmaAllocator allocator);
  void forEachInstance(auto fn) const {
    for (int i = 0; i < TREE_TEMPLATE_COUNT; i++)
      for (const auto &inst : _instances[i])
        fn(inst.pos, i);
  }
  void addTree(glm::vec3 pos, float yaw, float scale, int templateIdx);
  void clearTrees();
  void removeTreesInChunk(int chunkX, int chunkZ);
  void removeTree(float wx, float wz) {
    for (auto &vec : _instances) {
      vec.erase(std::remove_if(vec.begin(), vec.end(),
                               [&](const TreeRenderInstance &t) {
                                 return std::abs(t.pos.x - wx) < 1.f &&
                                        std::abs(t.pos.z - wz) < 1.f;
                               }),
                vec.end());
    }
    _instDirty = true;
  }

  void startFall(float wx, float wz);

  void rebuildMeshes();
  void update(float dt) {
    windTime += dt;

    for (auto &ft : _fallingTrees) {
      if (ft.done)
        continue;
      ft.age += dt;
      // Accelerate as it falls (gravity feel)
      float speed = 40.f + ft.fallAngle * 0.8f;
      ft.fallAngle += speed * dt;
      if (ft.fallAngle >= 90.f) {
        ft.fallAngle = 90.f;
        ft.done = true;
      }
    }
    _fallingTrees.erase(std::remove_if(_fallingTrees.begin(),
                                       _fallingTrees.end(),
                                       [](const FallingTree &ft) {
                                         return ft.done && ft.age > 2.f;
                                       }),
                        _fallingTrees.end());
  }

  void draw(VkCommandBuffer cmd, const glm::mat4 &viewProj, VkExtent2D extent,
            glm::vec3 camPos, glm::vec3 sunDir, float sunIntensity,
            float fogStart, float fogEnd) const;

private:
  VkDevice _device = VK_NULL_HANDLE;
  VmaAllocator _allocator = nullptr;
  VkCommandPool _pool = VK_NULL_HANDLE;
  VkQueue _queue = VK_NULL_HANDLE;
  std::array<TreeGpuMesh, TREE_TEMPLATE_COUNT> _meshes{};
  std::array<std::vector<TreeRenderInstance>, TREE_TEMPLATE_COUNT> _instances;
  std::array<VkBuffer, TREE_TEMPLATE_COUNT> _instBuf{};
  std::array<VmaAllocation, TREE_TEMPLATE_COUNT> _instAlloc{};
  std::array<uint32_t, TREE_TEMPLATE_COUNT> _instCount{};
  bool _instDirty = true;
  static std::vector<uint32_t> loadSpv(const char *path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
      throw std::runtime_error(std::string("Cannot open: ") + path);
    size_t sz = (size_t)f.tellg();
    std::vector<uint32_t> buf(sz / 4);
    f.seekg(0);
    f.read((char *)buf.data(), sz);
    return buf;
  }

  static VkShaderModule makeMod(VkDevice dev, const std::vector<uint32_t> &c) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = c.size() * 4;
    ci.pCode = c.data();
    VkShaderModule m;
    vkCreateShaderModule(dev, &ci, nullptr, &m);
    return m;
  }
  void createFallPipelines(VkDevice device, VkRenderPass rp, const char *tvs,
                           const char *lfs);

  std::vector<FallingTree> _fallingTrees;
  VkPipeline _fallTrunkPipeline = VK_NULL_HANDLE;
  VkPipelineLayout _fallTrunkLayout = VK_NULL_HANDLE;
  VkPipeline _fallLeafPipeline = VK_NULL_HANDLE;
  VkPipelineLayout _fallLeafLayout = VK_NULL_HANDLE;
  VkPipeline _trunkPipeline = VK_NULL_HANDLE;
  VkPipelineLayout _trunkLayout = VK_NULL_HANDLE;
  VkPipeline _leafPipeline = VK_NULL_HANDLE;
  VkPipelineLayout _leafLayout = VK_NULL_HANDLE;
  VkBuffer _fallInstBuf = VK_NULL_HANDLE;
  VmaAllocation _fallInstAlloc = nullptr;
  uint32_t _fallInstCap = 0;
  void uploadInstances(VkCommandPool pool, VkQueue queue);
  void uploadMesh(VkCommandPool pool, VkQueue queue, int idx,
                  const TreeMeshData &data);
  void createPipelines(VkDevice device, VkRenderPass rp, VkExtent2D ext,
                       const char *tvs, const char *tfs, const char *lvs,
                       const char *lfs);

  static VkBuffer uploadBuf(VkDevice dev, VmaAllocator alloc,
                            VkCommandPool pool, VkQueue q, const void *data,
                            VkDeviceSize size, VkBufferUsageFlags usage,
                            VmaAllocation &outAlloc);
};
