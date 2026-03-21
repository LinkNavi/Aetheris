#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <fstream>
#include <stdexcept>
#include "day_night.h"

struct SkyPC {
    glm::mat4 invViewProj; // inverse(proj * mat4(mat3(view)))
    glm::vec4 sunDir;
    glm::vec4 params;      // x=sunIntensity, y=time, z=cloudSpeed, w=0
    glm::vec4 camPos;
};

struct SkyGodRayRenderer {
    VkPipeline       skyPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyLayout   = VK_NULL_HANDLE;
    float            cloudTime   = 0.f;

    void update(float dt) { cloudTime += dt; }

    void init(VkDevice device, VmaAllocator,
              VkRenderPass renderPass, VkExtent2D,
              const char* skyVert, const char* skyFrag) {

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.size       = sizeof(SkyPC);

        VkPipelineLayoutCreateInfo li{};
        li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges    = &pcr;
        vkCreatePipelineLayout(device, &li, nullptr, &skyLayout);

        auto vc = loadSpv(skyVert);
        auto fc = loadSpv(skyFrag);
        VkShaderModule vm = makeMod(device, vc);
        VkShaderModule fm = makeMod(device, fc);

        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        st[0].module = vm; st[0].pName = "main";
        st[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        st[1].module = fm; st[1].pName = "main";

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

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
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
        pi.layout              = skyLayout;
        pi.renderPass          = renderPass;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &skyPipeline);

        vkDestroyShaderModule(device, vm, nullptr);
        vkDestroyShaderModule(device, fm, nullptr);
    }

    void destroy(VkDevice device, VmaAllocator) {
        if (skyPipeline) vkDestroyPipeline(device, skyPipeline, nullptr);
        if (skyLayout)   vkDestroyPipelineLayout(device, skyLayout, nullptr);
        skyPipeline = VK_NULL_HANDLE;
        skyLayout   = VK_NULL_HANDLE;
    }

    // viewMatrix = camera.view(), projMatrix = camera.proj(aspect)
    void drawSky(VkCommandBuffer cmd, VkExtent2D extent,
                 const glm::mat4& viewMatrix,
                 const glm::mat4& projMatrix,
                 const DayNight& dn,
                 glm::vec3 camPos) const {
        if (!skyPipeline) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);

        VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0.f, 1.f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0,0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        SkyPC pc;
        // Build rotation-only view (strip translation), combine with projection,
        // then invert. This lets the vertex shader reconstruct proper world-space
        // ray directions from NDC coordinates, accounting for FOV and aspect ratio.
        glm::mat4 viewRot = glm::mat4(glm::mat3(viewMatrix));
        pc.invViewProj    = glm::inverse(projMatrix * viewRot);
        pc.sunDir         = glm::vec4(dn.sunDir(), 0.f);
        pc.params         = glm::vec4(dn.sunIntensity(), cloudTime, 1.f, 0.f);
        pc.camPos         = glm::vec4(camPos, 0.f);

        vkCmdPushConstants(cmd, skyLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(SkyPC), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Keep old signature for backward compat but add proj overload
    void drawSky(VkCommandBuffer cmd, VkExtent2D extent,
                 const glm::mat4& viewMatrix,
                 const DayNight& dn,
                 glm::vec3 camPos) const {
        // Fallback: reconstruct a reasonable projection
        float aspect = (float)extent.width / std::max((float)extent.height, 1.f);
        glm::mat4 proj = glm::perspective(glm::radians(70.f), aspect, 0.05f, 1000.f);
        proj[1][1] *= -1; // Vulkan Y flip
        drawSky(cmd, extent, viewMatrix, proj, dn, camPos);
    }

    void drawGodRays(VkCommandBuffer, VkExtent2D, const glm::mat4&, const DayNight&) const {}

private:
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
