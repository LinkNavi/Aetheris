#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include "spell_element.h"
#include "day_night.h"
#include <cstring>
struct Decal {
    glm::vec3    pos;
    glm::vec3    normal;
    float        radius;
    float        age;
    float        lifetime;
    SpellElement element;
    float        complexity; // 0-1, derived from spell source line count
    float        manaSpent;  // raw mana, drives glow
    bool         dead = false;
};

struct DecalVertex {
    glm::vec3 pos;
    glm::vec2 uv;
};

struct DecalPC {
    glm::mat4 viewProj;
    glm::vec4 color;      // core element colour
    glm::vec4 glowColor;  // halo element colour
    glm::vec4 params;     // x=age y=lifetime z=element w=complexity
    glm::vec4 mana;       // x=manaSpent
};

class DecalRenderer {
public:
    static constexpr int MAX_DECALS = 64;

    void init(VkDevice device, VmaAllocator allocator,
              VkCommandPool pool, VkQueue queue,
              VkRenderPass renderPass,
              const char* vertSpv, const char* fragSpv) {
        _device    = device;
        _allocator = allocator;

        // Upload unit quad — we bake world positions per decal each frame
        // into a dynamic vertex buffer
        _dynVBufSize = MAX_DECALS * 4 * sizeof(DecalVertex);
        {
            VkBufferCreateInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size  = _dynVBufSize;
            bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo info{};
            vmaCreateBuffer(allocator, &bi, &ai, &_vBuf, &_vAlloc, &info);
            _vMapped = info.pMappedData;
        }

        // Static index buffer: quads → triangles, MAX_DECALS quads
        std::vector<uint32_t> inds;
        inds.reserve(MAX_DECALS * 6);
        for (int i = 0; i < MAX_DECALS; i++) {
            uint32_t b = i * 4;
            inds.insert(inds.end(), {b,b+1,b+2, b,b+2,b+3});
        }
        _iBuf = uploadBuf(device, allocator, pool, queue,
                          inds.data(), inds.size() * sizeof(uint32_t),
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, _iAlloc);

        // Pipeline
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.size       = sizeof(DecalPC);

        VkPipelineLayoutCreateInfo li{};
        li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges    = &pcr;
        vkCreatePipelineLayout(device, &li, nullptr, &_layout);

        auto vc = loadSpv(vertSpv);
        auto fc = loadSpv(fragSpv);
        VkShaderModule vm = makeMod(device, vc);
        VkShaderModule fm = makeMod(device, fc);

        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vm, "main"};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main"};

        VkVertexInputBindingDescription vb{0, sizeof(DecalVertex),
                                           VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription va[2]{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DecalVertex, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(DecalVertex, uv)},
        };
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;   vi.pVertexBindingDescriptions   = &vb;
        vi.vertexAttributeDescriptionCount = 2;   vi.pVertexAttributeDescriptions = va;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dynStates;

        VkPipelineViewportStateCreateInfo vps{};
        vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1; vps.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Depth test on, no write — sits on surface
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType           = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE;
        ds.depthWriteEnable = VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        // Additive blend — glows without covering terrain
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask      = 0xF;
        ba.blendEnable         = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; // additive
        ba.colorBlendOp        = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ba.alphaBlendOp        = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo bl{};
        bl.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        bl.attachmentCount = 1;
        bl.pAttachments    = &ba;

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount          = 2;   pi.pStages            = st;
        pi.pVertexInputState   = &vi; pi.pInputAssemblyState = &ia;
        pi.pViewportState      = &vps;pi.pRasterizationState = &rs;
        pi.pMultisampleState   = &ms; pi.pDepthStencilState  = &ds;
        pi.pColorBlendState    = &bl; pi.pDynamicState       = &dyn;
        pi.layout              = _layout;
        pi.renderPass          = renderPass;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &_pipeline);

        vkDestroyShaderModule(device, vm, nullptr);
        vkDestroyShaderModule(device, fm, nullptr);
    }

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (_pipeline) vkDestroyPipeline(device, _pipeline, nullptr);
        if (_layout)   vkDestroyPipelineLayout(device, _layout, nullptr);
        if (_vBuf)     vmaDestroyBuffer(allocator, _vBuf, _vAlloc);
        if (_iBuf)     vmaDestroyBuffer(allocator, _iBuf, _iAlloc);
    }

    // complexity: pass spell source code, we count lines
    void spawn(glm::vec3 pos, glm::vec3 normal, float radius,
               SpellElement element, float manaSpent,
               const std::string& spellSource,
               float lifetime = 30.f) {
        if (_decals.size() >= MAX_DECALS)
            _decals.erase(_decals.begin()); // evict oldest

        // Count non-empty, non-comment lines as complexity
        int lines = 0;
        std::istringstream ss(spellSource);
        std::string line;
        while (std::getline(ss, line)) {
            // Strip leading whitespace
            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            std::string trimmed = line.substr(start);
            if (trimmed.empty()) continue;
            if (trimmed.substr(0, 2) == "//") continue;
            lines++;
        }
        // Map line count to 0-1: 1 line = 0.0, 20+ lines = 1.0
        float complexity = std::clamp((float)(lines - 1) / 19.f, 0.f, 1.f);

        _decals.push_back({pos, glm::normalize(normal), radius,
                           0.f, lifetime, element,
                           complexity, manaSpent, false});
    }

    void update(float dt) {
        for (auto& d : _decals) {
            d.age += dt;
            if (d.age >= d.lifetime) d.dead = true;
        }
        _decals.erase(std::remove_if(_decals.begin(), _decals.end(),
                      [](const Decal& d){ return d.dead; }), _decals.end());
    }

    void draw(VkCommandBuffer cmd, VkExtent2D extent,
              const glm::mat4& viewProj, const DayNight& dn) const {
        if (!_pipeline || _decals.empty()) return;

        // Build world-space quads into mapped vertex buffer
        auto* verts = static_cast<DecalVertex*>(_vMapped);
        int   count = 0;

        for (const auto& d : _decals) {
            // Tangent frame from normal
            glm::vec3 up    = std::abs(d.normal.y) < 0.9f
                            ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
            glm::vec3 right = glm::normalize(glm::cross(up, d.normal));
            glm::vec3 fwd   = glm::normalize(glm::cross(d.normal, right));
            glm::vec3 base  = d.pos + d.normal * 0.03f; // lift off surface

            verts[count*4+0] = {base + (-right - fwd) * d.radius, {0,0}};
            verts[count*4+1] = {base + ( right - fwd) * d.radius, {1,0}};
            verts[count*4+2] = {base + ( right + fwd) * d.radius, {1,1}};
            verts[count*4+3] = {base + (-right + fwd) * d.radius, {0,1}};
            count++;
        }
        vmaFlushAllocation(_allocator, _vAlloc, 0, count * 4 * sizeof(DecalVertex));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        VkViewport vp{0,0,(float)extent.width,(float)extent.height,0.f,1.f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0,0},extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &_vBuf, &zero);
        vkCmdBindIndexBuffer(cmd, _iBuf, 0, VK_INDEX_TYPE_UINT32);

        for (int i = 0; i < count; i++) {
            const Decal& d = _decals[i];
            const ElementVisual& ev = getElementVisual(d.element);

            DecalPC pc{};
            pc.viewProj  = viewProj;
            pc.color     = {ev.r,    ev.g,    ev.b,    1.f};
            pc.glowColor = {ev.glowR,ev.glowG,ev.glowB,1.f};
            pc.params    = {d.age, d.lifetime, (float)d.element, d.complexity};
            pc.mana      = {d.manaSpent, 0.f, 0.f, 0.f};

            vkCmdPushConstants(cmd, _layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(DecalPC), &pc);
            vkCmdDrawIndexed(cmd, 6, 1, i * 6, 0, 0);
        }
    }

private:
    VkDevice      _device    = VK_NULL_HANDLE;
    VmaAllocator  _allocator = nullptr;
    VkPipeline    _pipeline  = VK_NULL_HANDLE;
    VkPipelineLayout _layout = VK_NULL_HANDLE;
    VkBuffer      _vBuf      = VK_NULL_HANDLE;
    VmaAllocation _vAlloc    = nullptr;
    void*         _vMapped   = nullptr;
    VkDeviceSize  _dynVBufSize = 0;
    VkBuffer      _iBuf      = VK_NULL_HANDLE;
    VmaAllocation _iAlloc    = nullptr;
    std::vector<Decal> _decals;

    static VkBuffer uploadBuf(VkDevice dev, VmaAllocator alloc,
                               VkCommandPool pool, VkQueue q,
                               const void* data, VkDeviceSize size,
                               VkBufferUsageFlags usage, VmaAllocation& outAlloc) {
        VkBuffer stg; VmaAllocation sa;
        VkBufferCreateInfo b{}; b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = size; b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo a{}; a.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        a.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT; VmaAllocationInfo info{};
        vmaCreateBuffer(alloc,&b,&a,&stg,&sa,&info); memcpy(info.pMappedData,data,size);

        VkBuffer dst;
        b.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        a.usage = VMA_MEMORY_USAGE_GPU_ONLY; a.flags = 0;
        vmaCreateBuffer(alloc,&b,&a,&dst,&outAlloc,nullptr);

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd; vkAllocateCommandBuffers(dev,&ai,&cmd);
        VkCommandBufferBeginInfo bi2{};
        bi2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi2.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd,&bi2);
        VkBufferCopy r{0,0,size}; vkCmdCopyBuffer(cmd,stg,dst,1,&r);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount=1; si.pCommandBuffers=&cmd;
        vkQueueSubmit(q,1,&si,VK_NULL_HANDLE); vkQueueWaitIdle(q);
        vkFreeCommandBuffers(dev,pool,1,&cmd); vmaDestroyBuffer(alloc,stg,sa);
        return dst;
    }

    static std::vector<uint32_t> loadSpv(const char* path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error(std::string("Cannot open: ") + path);
        size_t sz = f.tellg(); std::vector<uint32_t> buf(sz/4);
        f.seekg(0); f.read((char*)buf.data(), sz); return buf;
    }
    static VkShaderModule makeMod(VkDevice dev, const std::vector<uint32_t>& c) {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = c.size()*4; ci.pCode = c.data();
        VkShaderModule m; vkCreateShaderModule(dev,&ci,nullptr,&m); return m;
    }
};
