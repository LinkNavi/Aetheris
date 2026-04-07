#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <fstream>
#include <stdexcept>
#include "projectile_manager.h"
#include "spell_element.h"
#include "day_night.h"

struct ProjectilePC {
    glm::mat4 viewProj;
    glm::vec4 sphereWorldPos; // xyz=pos, w=innerRadius
    glm::vec4 color;          // rgb=core
    glm::vec4 glowColor;      // rgb=glow, a=age
    glm::vec4 camPos;
    glm::vec4 params;         // x=element, y=sunIntensity, z=outerRadius, w=0
};

class ProjectileRenderer {
public:
    void init(VkDevice device, VmaAllocator allocator,
              VkRenderPass renderPass, VkExtent2D extent,
              const char* vertSpv, const char* fragSpv) {
        _device = device;

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.size       = sizeof(ProjectilePC);

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
        st[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        st[0].module = vm; st[0].pName = "main";
        st[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        st[1].module = fm; st[1].pName = "main";

        // No vertex input — generated in vert shader
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dynStates;

        VkPipelineViewportStateCreateInfo vps{};
        vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1; vps.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Depth test on, depth write off (transparent outer sphere)
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE;
        ds.depthWriteEnable = VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        // Alpha blending
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
        bl.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        bl.attachmentCount = 1;
        bl.pAttachments    = &ba;

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount          = 2; pi.pStages = st;
        pi.pVertexInputState   = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState      = &vps;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState   = &ms;
        pi.pDepthStencilState  = &ds;
        pi.pColorBlendState    = &bl;
        pi.pDynamicState       = &dyn;
        pi.layout              = _layout;
        pi.renderPass          = renderPass;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &_pipeline);

        vkDestroyShaderModule(device, vm, nullptr);
        vkDestroyShaderModule(device, fm, nullptr);
    }

    void destroy() {
        if (_pipeline) vkDestroyPipeline(_device, _pipeline, nullptr);
        if (_layout)   vkDestroyPipelineLayout(_device, _layout, nullptr);
        _pipeline = VK_NULL_HANDLE;
        _layout   = VK_NULL_HANDLE;
    }

    void draw(VkCommandBuffer cmd, VkExtent2D extent,
              const glm::mat4& viewProj, glm::vec3 camPos,
              const ProjectileManager& mgr, const DayNight& dn) const {
        if (!_pipeline || mgr.all().empty()) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

        VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0.f, 1.f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0,0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        for (const auto& p : mgr.all()) {
            const ElementVisual& ev = getElementVisual(p.element);

            ProjectilePC pc{};
            pc.viewProj       = viewProj;
            pc.sphereWorldPos = {p.pos.x, p.pos.y, p.pos.z, p.radius * 0.5f};
            pc.color          = {ev.r, ev.g, ev.b, 1.f};
            pc.glowColor      = {ev.glowR, ev.glowG, ev.glowB, p.age};
            pc.camPos         = {camPos.x, camPos.y, camPos.z, 0.f};
            pc.params         = {(float)p.element, dn.sunIntensity(),
                                  p.radius, 0.f};

            vkCmdPushConstants(cmd, _layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(ProjectilePC), &pc);


            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }

private:
    VkDevice         _device   = VK_NULL_HANDLE;
    VkPipeline       _pipeline = VK_NULL_HANDLE;
    VkPipelineLayout _layout   = VK_NULL_HANDLE;

    static std::vector<uint32_t> loadSpv(const char* path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error(std::string("Cannot open: ") + path);
        size_t sz = (size_t)f.tellg();
        std::vector<uint32_t> buf(sz/4);
        f.seekg(0); f.read((char*)buf.data(), sz);
        return buf;
    }

    static VkShaderModule makeMod(VkDevice dev, const std::vector<uint32_t>& c) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = c.size()*4; ci.pCode = c.data();
        VkShaderModule m;
        vkCreateShaderModule(dev, &ci, nullptr, &m);
        return m;
    }
};
