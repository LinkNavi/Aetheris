#include "view_model.h"
#include "log.h"
#include <fstream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

static void vmCheck(VkResult r, const char* msg) {
    if (r != VK_SUCCESS) throw std::runtime_error(msg);
}

static std::vector<uint32_t> vmLoadSpv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error(std::string("Cannot open shader: ") + path);
    size_t sz = (size_t)f.tellg();
    std::vector<uint32_t> buf(sz / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static VkShaderModule vmMakeModule(VkDevice dev, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * 4;
    ci.pCode    = code.data();
    VkShaderModule m;
    vmCheck(vkCreateShaderModule(dev, &ci, nullptr, &m), "viewmodel shader module");
    return m;
}

// Upload a CPU buffer to a GPU-only VkBuffer via a staging buffer
static void uploadBuffer(VkDevice device, VmaAllocator allocator,
                         VkCommandPool pool, VkQueue queue,
                         const void* data, VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkBuffer& outBuf, VmaAllocation& outAlloc)
{
    // Staging
    VkBuffer      stageBuf;
    VmaAllocation stageAlloc;
    {
        VkBufferCreateInfo bCI{};
        bCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bCI.size  = size;
        bCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aCI{};
        aCI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        aCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        vmaCreateBuffer(allocator, &bCI, &aCI, &stageBuf, &stageAlloc, &info);
        memcpy(info.pMappedData, data, size);
    }

    // GPU buffer
    {
        VkBufferCreateInfo bCI{};
        bCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bCI.size  = size;
        bCI.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aCI{};
        aCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(allocator, &bCI, &aCI, &outBuf, &outAlloc, nullptr);
    }

    // One-shot copy
    VkCommandBufferAllocateInfo cbAI{};
    cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAI.commandPool        = pool;
    cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cbAI, &cmd);

    VkCommandBufferBeginInfo bI{};
    bI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bI);
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, stageBuf, outBuf, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
    vmaDestroyBuffer(allocator, stageBuf, stageAlloc);
}

void ViewModelRenderer::init(VkDevice device, VmaAllocator /*allocator*/,
                              VkRenderPass renderPass, VkExtent2D extent,
                              const char* vertSpv, const char* fragSpv)
{
    // ── Push constant: single mat4 (model-view-proj) ──────────────────────
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges    = &pushRange;
    vmCheck(vkCreatePipelineLayout(device, &layoutCI, nullptr, &pipelineLayout),
            "viewmodel pipeline layout");

    // ── Shaders ───────────────────────────────────────────────────────────
    auto vertCode = vmLoadSpv(vertSpv);
    auto fragCode = vmLoadSpv(fragSpv);
    VkShaderModule vertMod = vmMakeModule(device, vertCode);
    VkShaderModule fragMod = vmMakeModule(device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    // ── Vertex input: pos(vec3) + normal(vec3) + uv(vec2) ────────────────
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(GltfVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GltfVertex, uv)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0.f, 1.f};
    VkRect2D   sc{{0,0}, extent};
    VkPipelineViewportStateCreateInfo vs{};
    vs.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1; vs.pViewports = &vp;
    vs.scissorCount  = 1; vs.pScissors  = &sc;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_BACK_BIT;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test ON so it occludes world geometry, but writes OFF so the hand
    // doesn't block itself when drawn after terrain. We clear depth to 1.0
    // before drawing the viewmodel so it always renders on top of terrain.
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_FALSE; // always on top
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAtt;

    VkGraphicsPipelineCreateInfo pCI{};
    pCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pCI.stageCount          = 2;
    pCI.pStages             = stages;
    pCI.pVertexInputState   = &vertexInput;
    pCI.pInputAssemblyState = &ia;
    pCI.pViewportState      = &vs;
    pCI.pRasterizationState = &raster;
    pCI.pMultisampleState   = &ms;
    pCI.pDepthStencilState  = &ds;
    pCI.pColorBlendState    = &blend;
    pCI.layout              = pipelineLayout;
    pCI.renderPass          = renderPass;
    vmCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pCI, nullptr, &pipeline),
            "viewmodel pipeline");

    vkDestroyShaderModule(device, vertMod, nullptr);
    vkDestroyShaderModule(device, fragMod, nullptr);

    Log::info("ViewModelRenderer initialised");
}

int ViewModelRenderer::loadMesh(VkDevice device, VmaAllocator allocator,
                                 VkCommandPool pool, VkQueue queue,
                                 const GltfModel& model,
                                 ViewModelTransform transform)
{
    if (!model.valid || model.meshes.empty()) return -1;

    // Merge all meshes in the GLB into one draw call
    std::vector<GltfVertex> verts;
    std::vector<uint32_t>   inds;
    for (auto& m : model.meshes) {
        uint32_t base = (uint32_t)verts.size();
        verts.insert(verts.end(), m.vertices.begin(), m.vertices.end());
        for (auto i : m.indices) inds.push_back(base + i);
    }

    ViewModelMesh gpu{};
    gpu.indexCount = (uint32_t)inds.size();

    uploadBuffer(device, allocator, pool, queue,
                 verts.data(), verts.size() * sizeof(GltfVertex),
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 gpu.vertBuf, gpu.vertAlloc);

    uploadBuffer(device, allocator, pool, queue,
                 inds.data(), inds.size() * sizeof(uint32_t),
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 gpu.idxBuf, gpu.idxAlloc);

    meshes.push_back(gpu);
    transforms.push_back(transform);
    return (int)meshes.size() - 1;
}

void ViewModelRenderer::draw(VkCommandBuffer cmd, const glm::mat4& proj) const {
    if (activeMesh < 0 || activeMesh >= (int)meshes.size()) return;

    const auto& mesh = meshes[activeMesh];
    const auto& t    = transforms[activeMesh];

    // Build model matrix in view space (identity view = camera space)
    glm::mat4 model = glm::mat4(1.f);
    model = glm::translate(model, t.offset);
    model = model * glm::eulerAngleXYZ(
        glm::radians(t.rotation.x),
        glm::radians(t.rotation.y),
        glm::radians(t.rotation.z));
    model = glm::scale(model, t.scale);

    glm::mat4 mvp = proj * model;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertBuf, &zero);
    vkCmdBindIndexBuffer(cmd, mesh.idxBuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::mat4), &mvp);
    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}

void ViewModelRenderer::destroy(VkDevice device, VmaAllocator allocator) {
    for (auto& m : meshes) {
        vmaDestroyBuffer(allocator, m.vertBuf,  m.vertAlloc);
        vmaDestroyBuffer(allocator, m.idxBuf,   m.idxAlloc);
    }
    meshes.clear();
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
}
