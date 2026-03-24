#include "tree_renderer.h"
#include "log.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <string>
// ── Helpers
// ───────────────────────────────────────────────────────────────────

static float rng(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return (float)(s & 0xffff) / 65535.f;
}

static float barkNoise(float x, float y, float z) {
  float ring = std::sin(std::sqrt(x * x + z * z) * 2.2f + y * 0.5f);
  float grain = std::sin(y * 5.f + x * 0.9f) * 0.5f;
  return (ring * 0.6f + grain * 0.4f) * 0.5f + 0.5f;
}

static float leafNoise(float x, float y, float z) {
  float a = std::sin(x * 2.3f) * std::cos(z * 2.1f) * std::sin(y * 2.5f);
  float b = std::cos(x * 3.7f + 0.7f) * std::sin(z * 3.3f) * 0.5f;
  return (a + b) * 0.5f + 0.5f;
}

// Build a tapered cylinder segment (bottom cap to top cap)
// Appends to verts/indices
static void addCylinder(std::vector<TreeVertex> &verts,
                        std::vector<uint32_t> &inds, glm::vec3 base,
                        glm::vec3 tip, float rBase, float rTip, int sides) {
  uint32_t startIdx = (uint32_t)verts.size();

  glm::vec3 axis = glm::normalize(tip - base);
  // Build a local frame perpendicular to axis
  glm::vec3 up =
      std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 right = glm::normalize(glm::cross(up, axis));
  glm::vec3 fwd = glm::cross(axis, right);

  float segLen = glm::length(tip - base);

  for (int s = 0; s <= sides; s++) {
    float angle = (float)s / (float)sides * 6.2831853f;
    float cx = std::cos(angle), cz = std::sin(angle);
    glm::vec3 dir = right * cx + fwd * cz;

    // Base ring
    glm::vec3 bPos = base + dir * rBase;
    glm::vec3 tPos = tip + dir * rTip;

    // Normal points outward from axis, slightly upward along taper
    glm::vec3 norm = glm::normalize(dir + axis * (rBase - rTip) / segLen);

    float u = (float)s / (float)sides;

    TreeVertex vb, vt;
    vb.pos = bPos;
    vb.normal = norm;
    vb.uv = {u, 0.f};
    vb.noiseVal = barkNoise(bPos.x, bPos.y, bPos.z);

    vt.pos = tPos;
    vt.normal = norm;
    vt.uv = {u, 1.f};
    vt.noiseVal = barkNoise(tPos.x, tPos.y, tPos.z);

    verts.push_back(vb);
    verts.push_back(vt);
  }

  // Quad strip
  for (int s = 0; s < sides; s++) {
    uint32_t b0 = startIdx + s * 2;
    uint32_t t0 = b0 + 1;
    uint32_t b1 = b0 + 2;
    uint32_t t1 = b0 + 3;

    inds.push_back(b0);
    inds.push_back(t0);
    inds.push_back(b1);
    inds.push_back(b1);
    inds.push_back(t0);
    inds.push_back(t1);
  }
}

// Add a cluster of billboard leaf quads around a point
static void addLeafCluster(std::vector<LeafVertex> &verts,
                           std::vector<uint32_t> &inds, glm::vec3 center,
                           float radius, int count, uint32_t &seed) {
  for (int i = 0; i < count; i++) {
    // Random position within sphere
    float rx = (rng(seed) - 0.5f) * 1.4f * radius;
    float ry = (rng(seed) - 0.5f) * 1.4f * radius;
    float rz = (rng(seed) - 0.5f) * 1.4f * radius;
    glm::vec3 pos = center + glm::vec3(rx, ry, rz);

    // Random orientation for the quad
    float yaw = rng(seed) * 6.2831853f;
    float pitch = (rng(seed) - 0.5f) * 1.2f;
    float size = 1.4f + rng(seed) * 1.0f;

    float cy = std::cos(yaw), sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);

    // Two axes of the quad
    glm::vec3 qRight = {cy, 0.f, -sy};
    glm::vec3 qUp = {sy * sp, cp, cy * sp};

    float noise = leafNoise(pos.x, pos.y, pos.z);
    float flutter = rng(seed) * 6.2831853f;

    uint32_t base = (uint32_t)verts.size();

    // 4 corners of the leaf quad
    glm::vec3 corners[4] = {
        pos - qRight * size - qUp * size,
        pos + qRight * size - qUp * size,
        pos + qRight * size + qUp * size,
        pos - qRight * size + qUp * size,
    };
    glm::vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (int c = 0; c < 4; c++) {
      LeafVertex v;
      v.pos = corners[c];
      v.uv = uvs[c];
      v.noiseVal = noise;
      v.flutter = flutter;
      verts.push_back(v);
    }

    // Two triangles — render both sides
    inds.push_back(base);
    inds.push_back(base + 1);
    inds.push_back(base + 2);
    inds.push_back(base);
    inds.push_back(base + 2);
    inds.push_back(base + 3);
    inds.push_back(base + 2);
    inds.push_back(base + 1);
    inds.push_back(base); // back face
    inds.push_back(base + 3);
    inds.push_back(base + 2);
    inds.push_back(base); // back face
  }
}

// ── Build tree mesh
// ───────────────────────────────────────────────────────────

TreeMeshData buildTreeMesh(int templateIdx, int sides, float trunkHeight,
                           float trunkRadiusBase, float trunkRadiusTip,
                           int branchCount, float canopyRadius, uint32_t seed) {
  TreeMeshData d;
  uint32_t s = seed;

  // ── Main trunk ────────────────────────────────────────────────────────────
  int trunkSegs = 3 + (int)(rng(s) * 2);
  glm::vec3 trunkBase(0.f);
  glm::vec3 trunkTip(0.f, trunkHeight, 0.f);

  // Slight lean/curve for organic feel
  float leanX = (rng(s) - 0.5f) * 0.3f;
  float leanZ = (rng(s) - 0.5f) * 0.3f;
  trunkTip += glm::vec3(leanX, 0.f, leanZ);

  // Build trunk in segments, each slightly offset for curve
  glm::vec3 prev = trunkBase;
  for (int seg = 0; seg < trunkSegs; seg++) {
    float t0 = (float)seg / (float)trunkSegs;
    float t1 = (float)(seg + 1) / (float)trunkSegs;

    glm::vec3 segBase = glm::mix(trunkBase, trunkTip, t0);
    glm::vec3 segTip = glm::mix(trunkBase, trunkTip, t1);

    // Add slight wobble per segment
    if (seg > 0 && seg < trunkSegs - 1) {
      segTip.x += (rng(s) - 0.5f) * 0.15f;
      segTip.z += (rng(s) - 0.5f) * 0.15f;
    }

    float r0 = glm::mix(trunkRadiusBase, trunkRadiusTip, t0);
    float r1 = glm::mix(trunkRadiusBase, trunkRadiusTip, t1);

    addCylinder(d.trunkVerts, d.trunkIndices, segBase, segTip, r0, r1, sides);
  }

  // ── Branches ──────────────────────────────────────────────────────────────
  float branchStartFrac =
      0.55f + rng(s) * 0.15f; // branches start 55-70% up trunk

  for (int b = 0; b < branchCount; b++) {
    float frac = branchStartFrac +
                 (float)b / (float)branchCount * (1.f - branchStartFrac);
    glm::vec3 branchBase = glm::mix(trunkBase, trunkTip, frac);

    // Branch angle — spread outward and slightly upward
    float bYaw = rng(s) * 6.2831853f;
    float bPitch = 0.3f + rng(s) * 0.4f; // 17-40 degrees up
    float bLen = trunkHeight * (0.3f + rng(s) * 0.3f);
    float bRadBase = trunkRadiusTip * (1.2f + rng(s) * 0.3f);
    float bRadTip = bRadBase * 0.3f;

    glm::vec3 bDir = {std::cos(bYaw) * std::cos(bPitch), std::sin(bPitch),
                      std::sin(bYaw) * std::cos(bPitch)};
    glm::vec3 branchTip = branchBase + bDir * bLen;

    addCylinder(d.trunkVerts, d.trunkIndices, branchBase, branchTip, bRadBase,
                bRadTip, sides);

    // Sub-branch from halfway along branch
    if (rng(s) > 0.4f) {
      glm::vec3 subBase = glm::mix(branchBase, branchTip, 0.5f + rng(s) * 0.3f);
      float sbYaw = bYaw + (rng(s) - 0.5f) * 1.5f;
      float sbPitch = bPitch + rng(s) * 0.3f;
      float sbLen = bLen * 0.5f;
      glm::vec3 sbDir = {std::cos(sbYaw) * std::cos(sbPitch), std::sin(sbPitch),
                         std::sin(sbYaw) * std::cos(sbPitch)};
      addCylinder(d.trunkVerts, d.trunkIndices, subBase,
                  subBase + sbDir * sbLen, bRadTip, bRadTip * 0.4f, sides);

      // Leaf cluster at sub-branch tip
      addLeafCluster(d.leafVerts, d.leafIndices, subBase + sbDir * sbLen,
                     canopyRadius * 0.6f, 20 + ( 5),
                     s); // was 8+6
    }

    // Leaf cluster at branch tip
    addLeafCluster(d.leafVerts, d.leafIndices, branchTip,
                   canopyRadius * (0.7f + rng(s) * 0.3f),
                   30 + (int)(1), s); // was 12+10
  }

  // Main canopy cluster at trunk top
  addLeafCluster(d.leafVerts, d.leafIndices,
                 trunkTip + glm::vec3(leanX, canopyRadius * 0.4f, leanZ),
                 canopyRadius * 1.2f, 60 + (int)(2), s); // was 20+15

  return d;
}

// ── Vulkan helpers
// ────────────────────────────────────────────────────────────

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

static VkShaderModule makeMod(VkDevice dev, const std::vector<uint32_t> &code) {
  VkShaderModuleCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ci.codeSize = code.size() * 4;
  ci.pCode = code.data();
  VkShaderModule m;
  vkCreateShaderModule(dev, &ci, nullptr, &m);
  return m;
}

VkBuffer TreeRenderer::uploadBuf(VkDevice dev, VmaAllocator alloc,
                                 VkCommandPool pool, VkQueue q,
                                 const void *data, VkDeviceSize size,
                                 VkBufferUsageFlags usage,
                                 VmaAllocation &outAlloc) {
  VkBuffer stg;
  VmaAllocation sa;
  {
    VkBufferCreateInfo b{};
    b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    b.size = size;
    b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo a{};
    a.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    a.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo i{};
    vmaCreateBuffer(alloc, &b, &a, &stg, &sa, &i);
    memcpy(i.pMappedData, data, size);
  }
  VkBuffer dst;
  {
    VkBufferCreateInfo b{};
    b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    b.size = size;
    b.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo a{};
    a.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateBuffer(alloc, &b, &a, &dst, &outAlloc, nullptr);
  }
  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  vkAllocateCommandBuffers(dev, &ai, &cmd);
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  VkBufferCopy r{0, 0, size};
  vkCmdCopyBuffer(cmd, stg, dst, 1, &r);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(q);
  vkFreeCommandBuffers(dev, pool, 1, &cmd);
  vmaDestroyBuffer(alloc, stg, sa);
  return dst;
}

void TreeRenderer::uploadMesh(VkCommandPool pool, VkQueue queue, int idx,
                              const TreeMeshData &data) {
  auto &m = _meshes[idx];

  if (!data.trunkVerts.empty()) {
    m.trunkVBuf =
        uploadBuf(_device, _allocator, pool, queue, data.trunkVerts.data(),
                  data.trunkVerts.size() * sizeof(TreeVertex),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m.trunkVAlloc);
    m.trunkIBuf =
        uploadBuf(_device, _allocator, pool, queue, data.trunkIndices.data(),
                  data.trunkIndices.size() * sizeof(uint32_t),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m.trunkIAlloc);
    m.trunkIdxCount = (uint32_t)data.trunkIndices.size();
  }

  if (!data.leafVerts.empty()) {
    m.leafVBuf =
        uploadBuf(_device, _allocator, pool, queue, data.leafVerts.data(),
                  data.leafVerts.size() * sizeof(LeafVertex),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m.leafVAlloc);
    m.leafIBuf =
        uploadBuf(_device, _allocator, pool, queue, data.leafIndices.data(),
                  data.leafIndices.size() * sizeof(uint32_t),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m.leafIAlloc);
    m.leafIdxCount = (uint32_t)data.leafIndices.size();
  }
}

// ── Pipeline creation
// ─────────────────────────────────────────────────────────

void TreeRenderer::createPipelines(VkDevice device, VkRenderPass rp,
                                   VkExtent2D ext, const char *tvs,
                                   const char *tfs, const char *lvs,
                                   const char *lfs) {
  // ── Shared push constant: mat4 viewProj + vec4(windTime,0,0,0) ──────────
  VkPushConstantRange pcr{};
  pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pcr.size = sizeof(glm::mat4) + sizeof(glm::vec4);

  VkPipelineLayoutCreateInfo li{};
  li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  li.pushConstantRangeCount = 1;
  li.pPushConstantRanges = &pcr;
  vkCreatePipelineLayout(device, &li, nullptr, &_trunkLayout);
  vkCreatePipelineLayout(device, &li, nullptr, &_leafLayout);

  // ── Dynamic state ─────────────────────────────────────────────────────────
  VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{};
  dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;

  VkPipelineViewportStateCreateInfo vps{};
  vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vps.viewportCount = 1;
  vps.scissorCount = 1;

  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_BACK_BIT;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState ba{};
  ba.colorWriteMask = 0xF;
  // Leaf pipeline uses alpha blend
  VkPipelineColorBlendAttachmentState baLeaf{};
  baLeaf.colorWriteMask = 0xF;
  baLeaf.blendEnable = VK_TRUE;
  baLeaf.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  baLeaf.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  baLeaf.colorBlendOp = VK_BLEND_OP_ADD;
  baLeaf.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  baLeaf.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  baLeaf.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo blTrunk{};
  blTrunk.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blTrunk.attachmentCount = 1;
  blTrunk.pAttachments = &ba;

  VkPipelineColorBlendStateCreateInfo blLeaf{};
  blLeaf.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blLeaf.attachmentCount = 1;
  blLeaf.pAttachments = &baLeaf;

  // ── Trunk pipeline ────────────────────────────────────────────────────────
  {
    auto vc = loadSpv(tvs), fc = loadSpv(tfs);
    VkShaderModule vm = makeMod(device, vc), fm = makeMod(device, fc);

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vm;
    st[0].pName = "main";
    st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = fm;
    st[1].pName = "main";

    // Vertex binding 0: TreeVertex (per-vertex)
    // Vertex binding 1: TreeInstance (per-instance)
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(TreeVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(TreeRenderInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    // TreeVertex attrs: pos(0), normal(1), uv(2), noiseVal(3)
    // TreeInstance attrs: pos(4), yaw(5), scale(6), seed(7)
    VkVertexInputAttributeDescription attrs[8]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TreeVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TreeVertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TreeVertex, uv)};
    attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(TreeVertex, noiseVal)};
    attrs[4] = {4, 1, VK_FORMAT_R32G32B32_SFLOAT,
                offsetof(TreeRenderInstance, pos)};
    attrs[5] = {5, 1, VK_FORMAT_R32_SFLOAT, offsetof(TreeRenderInstance, yaw)};
    attrs[6] = {6, 1, VK_FORMAT_R32_SFLOAT,
                offsetof(TreeRenderInstance, scale)};
    attrs[7] = {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(TreeRenderInstance, seed)};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 8;
    vi.pVertexAttributeDescriptions = attrs;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vps;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &blTrunk;
    pi.pDynamicState = &dyn;
    pi.layout = _trunkLayout;
    pi.renderPass = rp;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr,
                              &_trunkPipeline);

    vkDestroyShaderModule(device, vm, nullptr);
    vkDestroyShaderModule(device, fm, nullptr);
  }

  // ── Leaf pipeline (no back-face cull, alpha blend) ─────────────────────
  {
    auto vc = loadSpv(lvs), fc = loadSpv(lfs);
    VkShaderModule vm = makeMod(device, vc), fm = makeMod(device, fc);

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vm;
    st[0].pName = "main";
    st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = fm;
    st[1].pName = "main";

    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(LeafVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(TreeRenderInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    // LeafVertex: pos(0), uv(1), noiseVal(2), flutter(3)
    // TreeInstance: pos(4), yaw(5), scale(6), seed(7)
    VkVertexInputAttributeDescription attrs[8]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LeafVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LeafVertex, uv)};
    attrs[2] = {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(LeafVertex, noiseVal)};
    attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(LeafVertex, flutter)};
    attrs[4] = {4, 1, VK_FORMAT_R32G32B32_SFLOAT,
                offsetof(TreeRenderInstance, pos)};
    attrs[5] = {5, 1, VK_FORMAT_R32_SFLOAT, offsetof(TreeRenderInstance, yaw)};
    attrs[6] = {6, 1, VK_FORMAT_R32_SFLOAT,
                offsetof(TreeRenderInstance, scale)};
    attrs[7] = {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(TreeRenderInstance, seed)};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 8;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineRasterizationStateCreateInfo rasterLeaf = raster;
    rasterLeaf.cullMode = VK_CULL_MODE_NONE; // leaves render both sides

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vps;
    pi.pRasterizationState = &rasterLeaf;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &blLeaf;
    pi.pDynamicState = &dyn;
    pi.layout = _leafLayout;
    pi.renderPass = rp;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr,
                              &_leafPipeline);

    vkDestroyShaderModule(device, vm, nullptr);
    vkDestroyShaderModule(device, fm, nullptr);
  }
}

// ── Init / destroy
// ────────────────────────────────────────────────────────────

void TreeRenderer::init(VkDevice device, VmaAllocator allocator,
                        VkCommandPool pool, VkQueue queue,
                        VkRenderPass renderPass, VkExtent2D extent,
                        const char *tvs, const char *tfs, const char *lvs,
                        const char *lfs) {
  _device = device;
  _allocator = allocator;
  _pool = pool;
  _queue = queue;
  createPipelines(device, renderPass, extent, tvs, tfs, lvs, lfs);

  // Build template meshes
  // Each template varies in trunk height, branch count, canopy size
  struct Params {
    float h;
    float rb;
    float rt;
    int br;
    float cr;
  };
  Params params[TREE_TEMPLATE_COUNT] = {
      {6.f, 0.80f, 0.35f, 4, 2.2f}, {8.f, 0.95f, 0.42f, 5, 2.8f},
      {5.f, 0.70f, 0.30f, 3, 1.9f}, {10.f, 1.10f, 0.50f, 6, 3.4f},
      {7.f, 0.88f, 0.38f, 4, 2.5f}, {9.f, 1.00f, 0.45f, 5, 3.0f},
  };

  for (int i = 0; i < TREE_TEMPLATE_COUNT; i++) {
    auto &p = params[i];
    TreeMeshData mesh = buildTreeMesh(i, 4, p.h, p.rb, p.rt, p.br, p.cr,
                                      (uint32_t)(12345 + i * 9999));
    uploadMesh(pool, queue, i, mesh);
    Log::info("Tree template " + std::to_string(i) + ": " +
              std::to_string(_meshes[i].trunkIdxCount) + " trunk tris, " +
              std::to_string(_meshes[i].leafIdxCount) + " leaf tris");
  }
}

void TreeRenderer::destroy(VkDevice device, VmaAllocator allocator) {
  for (int i = 0; i < TREE_TEMPLATE_COUNT; i++) {
    auto &m = _meshes[i];
    if (m.trunkVBuf)
      vmaDestroyBuffer(allocator, m.trunkVBuf, m.trunkVAlloc);
    if (m.trunkIBuf)
      vmaDestroyBuffer(allocator, m.trunkIBuf, m.trunkIAlloc);
    if (m.leafVBuf)
      vmaDestroyBuffer(allocator, m.leafVBuf, m.leafVAlloc);
    if (m.leafIBuf)
      vmaDestroyBuffer(allocator, m.leafIBuf, m.leafIAlloc);
    if (_instBuf[i])
      vmaDestroyBuffer(allocator, _instBuf[i], _instAlloc[i]);
  }
  if (_trunkPipeline)
    vkDestroyPipeline(device, _trunkPipeline, nullptr);
  if (_trunkLayout)
    vkDestroyPipelineLayout(device, _trunkLayout, nullptr);
  if (_leafPipeline)
    vkDestroyPipeline(device, _leafPipeline, nullptr);
  if (_leafLayout)
    vkDestroyPipelineLayout(device, _leafLayout, nullptr);
}

// ── Instance management
// ───────────────────────────────────────────────────────

void TreeRenderer::addTree(glm::vec3 pos, float yaw, float scale,
                           int templateIdx) {
  if (templateIdx < 0 || templateIdx >= TREE_TEMPLATE_COUNT)
    return;
  _instances[templateIdx].push_back(
      {pos, yaw, scale, pos.x * 0.1f + pos.z * 0.07f});
  _instDirty = true;
}

void TreeRenderer::clearTrees() {
  for (auto &v : _instances)
    v.clear();
  _instDirty = true;
}

void TreeRenderer::removeTreesInChunk(int chunkX, int chunkZ) {
  float mn = (float)(chunkX * (int)ChunkData::SIZE);
  float mz = (float)(chunkZ * (int)ChunkData::SIZE);
  float mx = mn + (float)ChunkData::SIZE;
  float mzx = mz + (float)ChunkData::SIZE;
  for (auto &vec : _instances) {
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const TreeRenderInstance &t) {
                               return t.pos.x >= mn && t.pos.x < mx &&
                                      t.pos.z >= mz && t.pos.z < mzx;
                             }),
              vec.end());
  }
  _instDirty = true;
}

void TreeRenderer::uploadInstances(VkCommandPool pool, VkQueue queue) {
  for (int i = 0; i < TREE_TEMPLATE_COUNT; i++) {
    if (_instBuf[i]) {
      vmaDestroyBuffer(_allocator, _instBuf[i], _instAlloc[i]);
      _instBuf[i] = VK_NULL_HANDLE;
      _instAlloc[i] = nullptr;
    }
    _instCount[i] = (uint32_t)_instances[i].size();
    if (_instCount[i] == 0)
      continue;

    _instBuf[i] =
        uploadBuf(_device, _allocator, pool, queue, _instances[i].data(),
                  _instances[i].size() * sizeof(TreeRenderInstance),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, _instAlloc[i]);
  }
  _instDirty = false;
}

// ── Draw
// ──────────────────────────────────────────────────────────────────────

void TreeRenderer::draw(VkCommandBuffer cmd, const glm::mat4 &viewProj,
                        VkExtent2D extent) const {
  if (_instDirty) {
    const_cast<TreeRenderer *>(this)->uploadInstances(_pool, _queue);
  }

  VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0.f, 1.f};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  VkRect2D sc{{0, 0}, extent};
  vkCmdSetScissor(cmd, 0, 1, &sc);

  struct PC {
    glm::mat4 viewProj;
    glm::vec4 params;
  };
  PC pc{viewProj, {windTime, 0.f, 0.f, 0.f}};

  // ── Draw trunks ───────────────────────────────────────────────────────────
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trunkPipeline);
  vkCmdPushConstants(cmd, _trunkLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PC), &pc);

  for (int i = 0; i < TREE_TEMPLATE_COUNT; i++) {
    if (_instCount[i] == 0)
      continue;
    auto &m = _meshes[i];
    if (!m.trunkVBuf || !m.trunkIBuf || !_instBuf[i])
      continue;

    VkDeviceSize offsets[2] = {0, 0};
    VkBuffer bufs[2] = {m.trunkVBuf, _instBuf[i]};
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdBindIndexBuffer(cmd, m.trunkIBuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m.trunkIdxCount, _instCount[i], 0, 0, 0);
  }

  // ── Draw leaves ───────────────────────────────────────────────────────────
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _leafPipeline);
  vkCmdPushConstants(cmd, _leafLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PC), &pc);

  for (int i = 0; i < TREE_TEMPLATE_COUNT; i++) {
    if (_instCount[i] == 0)
      continue;
    auto &m = _meshes[i];
    if (!m.leafVBuf || !m.leafIBuf || !_instBuf[i])
      continue;

    VkDeviceSize offsets[2] = {0, 0};
    VkBuffer bufs[2] = {m.leafVBuf, _instBuf[i]};
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdBindIndexBuffer(cmd, m.leafIBuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m.leafIdxCount, _instCount[i], 0, 0, 0);
  }
}
