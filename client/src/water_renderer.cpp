#include "water_renderer.h"
#include "log.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

static std::vector<uint32_t> loadSpvW(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error(std::string("Cannot open: ") + path);
    size_t sz = (size_t)f.tellg();
    std::vector<uint32_t> buf(sz / 4);
    f.seekg(0); f.read((char*)buf.data(), sz);
    return buf;
}

static VkShaderModule makeModW(VkDevice dev, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * 4; ci.pCode = code.data();
    VkShaderModule m; vkCreateShaderModule(dev, &ci, nullptr, &m);
    return m;
}

VkBuffer WaterRenderer::uploadBuf(VkDevice dev, VmaAllocator alloc,
                                   VkCommandPool pool, VkQueue q,
                                   const void* data, VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   VmaAllocation& outAlloc) {
    VkBuffer stg; VmaAllocation sa;
    {
        VkBufferCreateInfo b{}; b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = size; b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo a{}; a.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        a.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT; VmaAllocationInfo i{};
        vmaCreateBuffer(alloc, &b, &a, &stg, &sa, &i);
        memcpy(i.pMappedData, data, size);
    }
    VkBuffer dst;
    {
        VkBufferCreateInfo b{}; b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = size; b.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo a{}; a.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(alloc, &b, &a, &dst, &outAlloc, nullptr);
    }
    VkCommandBufferAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(dev, &ai, &cmd);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy r{0,0,size}; vkCmdCopyBuffer(cmd, stg, dst, 1, &r);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE); vkQueueWaitIdle(q);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
    vmaDestroyBuffer(alloc, stg, sa);
    return dst;
}

void WaterRenderer::init(VkDevice device, VmaAllocator allocator,
                          VkCommandPool pool, VkQueue queue,
                          VkRenderPass renderPass, VkExtent2D extent,
                          const char* vertSpv, const char* fragSpv) {
    _device = device; _allocator = allocator; _pool = pool; _queue = queue;

    // Push constant: mat4 viewProj + vec4(waterTime, sunIntensity, camY, 0)
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(glm::mat4) + sizeof(glm::vec4) * 2;

    VkPipelineLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(device, &li, nullptr, &_layout);

    auto vc = loadSpvW(vertSpv), fc = loadSpvW(fragSpv);
    VkShaderModule vm = makeModW(device, vc), fm = makeModW(device, fc);

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vm; st[0].pName = "main";
    st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fm; st[1].pName = "main";

    VkVertexInputBindingDescription bd{};
    bd.stride = sizeof(WaterVertex); bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(WaterVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(WaterVertex, uv)};
    attrs[2] = {2, 0, VK_FORMAT_R32_SFLOAT,        offsetof(WaterVertex, depth)};
    attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT,        offsetof(WaterVertex, flow)};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bd;
    vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE; // visible from below too
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE; // water doesn't write depth — draw after terrain
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    // Alpha blend
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask      = 0xF;
    ba.blendEnable         = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp        = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    ba.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo bl{};
    bl.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    bl.attachmentCount = 1; bl.pAttachments = &ba;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2; pi.pStages = st;
    pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vps; pi.pRasterizationState = &raster;
    pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &bl; pi.pDynamicState = &dyn;
    pi.layout = _layout; pi.renderPass = renderPass;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &_pipeline);

    vkDestroyShaderModule(device, vm, nullptr);
    vkDestroyShaderModule(device, fm, nullptr);
    Log::info("WaterRenderer initialised");
}

void WaterRenderer::uploadChunk(VkDevice device, VmaAllocator allocator,
                                 VkCommandPool pool, VkQueue queue,
                                 const WaterMesh& mesh) {
    if (mesh.vertices.empty()) { removeChunk(device, allocator, mesh.coord); return; }

    // Remove old if exists
    removeChunk(device, allocator, mesh.coord);

    WaterGpuChunk gpu{};
    gpu.idxCount = (uint32_t)mesh.indices.size();
    gpu.vertBuf = uploadBuf(device, allocator, pool, queue,
        mesh.vertices.data(), mesh.vertices.size() * sizeof(WaterVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu.vertAlloc);
    gpu.idxBuf = uploadBuf(device, allocator, pool, queue,
        mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gpu.idxAlloc);
    _chunks[mesh.coord] = gpu;
}

void WaterRenderer::removeChunk(VkDevice device, VmaAllocator allocator,
                                  ChunkCoord coord) {
    auto it = _chunks.find(coord);
    if (it == _chunks.end()) return;
    vkDeviceWaitIdle(device);
    if (it->second.vertBuf) vmaDestroyBuffer(allocator, it->second.vertBuf, it->second.vertAlloc);
    if (it->second.idxBuf)  vmaDestroyBuffer(allocator, it->second.idxBuf,  it->second.idxAlloc);
    _chunks.erase(it);
}

void WaterRenderer::draw(VkCommandBuffer cmd, const glm::mat4& viewProj,
                          VkExtent2D extent, glm::vec3 camPos,
                          float sunIntensity) const {
    if (_chunks.empty()) return;

    VkViewport vp{0,0,(float)extent.width,(float)extent.height,0.f,1.f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0,0},extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    struct PC {
        glm::mat4 viewProj;
        glm::vec4 params;   // x=waterTime, y=sunIntensity, z=camY, w=0
        glm::vec4 camPos;
    };
    PC pc{ viewProj, {waterTime, sunIntensity, camPos.y, 0.f}, {camPos.x, camPos.y, camPos.z, 0.f} };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
    vkCmdPushConstants(cmd, _layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PC), &pc);

    for (auto& [coord, gpu] : _chunks) {
        if (!gpu.vertBuf || !gpu.idxBuf) continue;
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &gpu.vertBuf, &zero);
        vkCmdBindIndexBuffer(cmd, gpu.idxBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, gpu.idxCount, 1, 0, 0, 0);
    }
}

void WaterRenderer::destroy(VkDevice device, VmaAllocator allocator) {
    for (auto& [coord, gpu] : _chunks) {
        if (gpu.vertBuf) vmaDestroyBuffer(allocator, gpu.vertBuf, gpu.vertAlloc);
        if (gpu.idxBuf)  vmaDestroyBuffer(allocator, gpu.idxBuf,  gpu.idxAlloc);
    }
    _chunks.clear();
    if (_pipeline) vkDestroyPipeline(device, _pipeline, nullptr);
    if (_layout)   vkDestroyPipelineLayout(device, _layout, nullptr);
}
