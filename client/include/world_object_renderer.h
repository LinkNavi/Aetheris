#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include "gltf_loader.h"
#include "item.h"
#include <imgui.h>
#include <unordered_map>

struct PlacedObject {
    glm::vec3   pos;
    float       yaw;      // Y-axis rotation in degrees
    float       scale;
    int         meshIdx;  // index into _meshes
    std::string name;
};

struct WorldObjMesh {
    VkBuffer      vertBuf    = VK_NULL_HANDLE;
    VmaAllocation vertAlloc  = nullptr;
    VkBuffer      idxBuf     = VK_NULL_HANDLE;
    VmaAllocation idxAlloc   = nullptr;
    uint32_t      indexCount = 0;
};

struct WorldObjPC {
    glm::mat4 mvp;
    glm::vec4 color;  // tint, w=1
};

class WorldObjectRenderer {
public:
    // ── Public placement settings ────────────────────────────────────────────
    float placementScale = 1.0f;
    float placementYaw   = 0.0f;
    float placementHeightOffset = 0.1f; // Offset above surface
    int   activeMeshIdx  = 0;

    // Register a mesh as the visual for a given ItemID
    void registerItemMesh(ItemID id, int meshIdx) {
        _itemMeshMap[(int)id] = meshIdx;
    }

    // Returns the mesh index for a given ItemID, or -1 if not registered
    int meshIndexForItem(ItemID id) const {
        auto it = _itemMeshMap.find((int)id);
        if (it == _itemMeshMap.end()) return -1;
        return it->second;
    }

    // Set placement preview (call every frame while previewing, or clear with -1 meshIdx)
    void setPlacementPreview(glm::vec3 pos, float yaw, float scale, int meshIdx) {
        _previewPos = pos;
        _previewYaw = yaw;
        _previewScale = scale;
        _previewMeshIdx = meshIdx;
    }

    // Clear placement preview
    void clearPlacementPreview() {
        _previewMeshIdx = -1;
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────
    void init(VkDevice device, VmaAllocator allocator,
              VkCommandPool pool, VkQueue queue,
              VkRenderPass renderPass,
              const char* vertSpv, const char* fragSpv)
    {
        _device    = device;
        _allocator = allocator;

        // Push constant: WorldObjPC (mat4 + vec4 = 80 bytes), both stages
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset     = 0;
        pcr.size       = sizeof(WorldObjPC);

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
                 nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vm, "main"};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main"};

        // GltfVertex: pos(R32G32B32), normal(R32G32B32), uv(R32G32)
        VkVertexInputBindingDescription vb{};
        vb.binding   = 0;
        vb.stride    = sizeof(GltfVertex);
        vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription va[3]{};
        va[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, pos)};
        va[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, normal)};
        va[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GltfVertex, uv)};

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &vb;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions    = va;

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
        vps.viewportCount = 1;
        vps.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_BACK_BIT;
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        // Opaque — no blending
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        ba.blendEnable    = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo bl{};
        bl.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        bl.attachmentCount = 1;
        bl.pAttachments    = &ba;

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount          = 2;
        pi.pStages             = st;
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

    void destroy(VkDevice device, VmaAllocator allocator) {
        for (auto& m : _meshes) {
            if (m.vertBuf) vmaDestroyBuffer(allocator, m.vertBuf, m.vertAlloc);
            if (m.idxBuf)  vmaDestroyBuffer(allocator, m.idxBuf,  m.idxAlloc);
        }
        _meshes.clear();
        _meshNames.clear();
        _objects.clear();
        if (_pipeline) vkDestroyPipeline(device, _pipeline, nullptr);
        if (_layout)   vkDestroyPipelineLayout(device, _layout, nullptr);
        _pipeline = VK_NULL_HANDLE;
        _layout   = VK_NULL_HANDLE;
    }

    // Upload a GltfModel to GPU — returns meshIdx
    int loadMesh(VkDevice device, VmaAllocator allocator,
                 VkCommandPool pool, VkQueue queue,
                 const GltfModel& model, const std::string& name)
    {
        // Flatten all sub-meshes into one vertex/index buffer
        std::vector<GltfVertex> verts;
        std::vector<uint32_t>   inds;

        for (const auto& sub : model.meshes) {
            uint32_t base = (uint32_t)verts.size();
            verts.insert(verts.end(), sub.vertices.begin(), sub.vertices.end());
            for (auto idx : sub.indices)
                inds.push_back(base + idx);
        }

        if (verts.empty() || inds.empty())
            return -1;

        WorldObjMesh m{};
        m.indexCount = (uint32_t)inds.size();
        m.vertBuf    = uploadBuf(device, allocator, pool, queue,
                                 verts.data(), verts.size() * sizeof(GltfVertex),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m.vertAlloc);
        m.idxBuf     = uploadBuf(device, allocator, pool, queue,
                                 inds.data(), inds.size() * sizeof(uint32_t),
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m.idxAlloc);

        int idx = (int)_meshes.size();
        _meshes.push_back(m);
        _meshNames.push_back(name);
        return idx;
    }

    void place(glm::vec3 pos, float yaw, float scale, int meshIdx) {
        if (meshIdx < 0 || meshIdx >= (int)_meshes.size())
            return;
        std::string name = _meshNames[meshIdx] + "_" + std::to_string(_objects.size());
        _objects.push_back({pos, yaw, scale, meshIdx, name});
    }

    void clear() {
        _objects.clear();
    }

    void draw(VkCommandBuffer cmd, VkExtent2D extent,
              const glm::mat4& viewProj) const
    {
        if (!_pipeline) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

        VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0.f, 1.f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        // Draw placed objects
        for (const auto& obj : _objects) {
            if (obj.meshIdx < 0 || obj.meshIdx >= (int)_meshes.size())
                continue;
            const WorldObjMesh& mesh = _meshes[obj.meshIdx];

            // Build model matrix: translate * rotateY * scale
            glm::mat4 model = glm::translate(glm::mat4(1.f), obj.pos)
                            * glm::rotate(glm::mat4(1.f),
                                          glm::radians(obj.yaw),
                                          glm::vec3(0.f, 1.f, 0.f))
                            * glm::scale(glm::mat4(1.f),
                                         glm::vec3(obj.scale));

            WorldObjPC pc{};
            pc.mvp   = viewProj * model;
            pc.color = {1.f, 1.f, 1.f, 1.f};

            VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertBuf, &zero);
            vkCmdBindIndexBuffer(cmd, mesh.idxBuf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(cmd, _layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(WorldObjPC), &pc);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }

        // Draw placement preview if active
        if (_previewMeshIdx >= 0 && _previewMeshIdx < (int)_meshes.size()) {
            const WorldObjMesh& mesh = _meshes[_previewMeshIdx];

            glm::mat4 model = glm::translate(glm::mat4(1.f), _previewPos)
                            * glm::rotate(glm::mat4(1.f),
                                          glm::radians(_previewYaw),
                                          glm::vec3(0.f, 1.f, 0.f))
                            * glm::scale(glm::mat4(1.f),
                                         glm::vec3(_previewScale));

            WorldObjPC pc{};
            pc.mvp   = viewProj * model;
            // Semi-transparent green tint for preview
            pc.color = {0.5f, 1.0f, 0.5f, 0.6f};

            VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertBuf, &zero);
            vkCmdBindIndexBuffer(cmd, mesh.idxBuf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(cmd, _layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(WorldObjPC), &pc);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    // Draw a preview/ghost of an object at a specific position
    void drawPreview(VkCommandBuffer cmd, VkExtent2D extent,
                     const glm::mat4& viewProj,
                     glm::vec3 pos, float yaw, float scale, int meshIdx) const
    {
        if (!_pipeline || meshIdx < 0 || meshIdx >= (int)_meshes.size())
            return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

        VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0.f, 1.f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        const WorldObjMesh& mesh = _meshes[meshIdx];

        // Build model matrix: translate * rotateY * scale
        glm::mat4 model = glm::translate(glm::mat4(1.f), pos)
                        * glm::rotate(glm::mat4(1.f),
                                      glm::radians(yaw),
                                      glm::vec3(0.f, 1.f, 0.f))
                        * glm::scale(glm::mat4(1.f),
                                     glm::vec3(scale));

        WorldObjPC pc{};
        pc.mvp   = viewProj * model;
        // Semi-transparent green tint for preview
        pc.color = {0.5f, 1.0f, 0.5f, 0.6f};

        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertBuf, &zero);
        vkCmdBindIndexBuffer(cmd, mesh.idxBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, _layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(WorldObjPC), &pc);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    }

    void drawDebugUI() {
        if (!ImGui::Begin("World Objects")) { ImGui::End(); return; }

        // Active mesh selector
        if (!_meshNames.empty()) {
            std::vector<const char*> names;
            names.reserve(_meshNames.size());
            for (const auto& n : _meshNames) names.push_back(n.c_str());

            int safeIdx = (activeMeshIdx >= 0 && activeMeshIdx < (int)names.size())
                          ? activeMeshIdx : 0;
            if (ImGui::Combo("Mesh", &safeIdx, names.data(), (int)names.size()))
                activeMeshIdx = safeIdx;
        } else {
            ImGui::TextDisabled("No meshes loaded");
        }

        ImGui::SliderFloat("Scale",        &placementScale, 0.01f, 10.f);
        ImGui::SliderFloat("Yaw Offset",   &placementYaw,   0.f,  360.f);
        ImGui::SliderFloat("Height Offset", &placementHeightOffset, -2.f, 2.f);

        ImGui::Separator();
        ImGui::Text("Placed objects: %d", (int)_objects.size());

        if (ImGui::Button("Clear All"))
            _objects.clear();

        ImGui::Separator();

        for (int i = 0; i < (int)_objects.size(); i++) {
            const auto& o = _objects[i];
            ImGui::PushID(i);
            ImGui::Text("[%d] %s  pos=(%.1f, %.1f, %.1f)  yaw=%.1f  scale=%.2f",
                        i, o.name.c_str(),
                        o.pos.x, o.pos.y, o.pos.z,
                        o.yaw, o.scale);
            ImGui::PopID();
        }

        ImGui::End();
    }

private:
    VkDevice         _device    = VK_NULL_HANDLE;
    VmaAllocator     _allocator = nullptr;
    VkPipeline       _pipeline  = VK_NULL_HANDLE;
    VkPipelineLayout _layout    = VK_NULL_HANDLE;

    std::vector<WorldObjMesh>        _meshes;
    std::vector<std::string>         _meshNames;
    std::vector<PlacedObject>        _objects;
    std::unordered_map<int, int>     _itemMeshMap; // ItemID -> meshIdx

    // Placement preview state
    glm::vec3 _previewPos{0, 0, 0};
    float _previewYaw = 0.f;
    float _previewScale = 1.f;
    int _previewMeshIdx = -1;

    // ── Helpers ──────────────────────────────────────────────────────────────
    static VkBuffer uploadBuf(VkDevice dev, VmaAllocator alloc,
                              VkCommandPool pool, VkQueue q,
                              const void* data, VkDeviceSize size,
                              VkBufferUsageFlags usage, VmaAllocation& outAlloc)
    {
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
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = c.size() * 4;
        ci.pCode    = c.data();
        VkShaderModule m; vkCreateShaderModule(dev, &ci, nullptr, &m); return m;
    }
};
