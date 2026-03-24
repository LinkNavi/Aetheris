#include "view_model.h"
#include "log.h"
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <stdexcept>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

// ── Vulkan helpers ────────────────────────────────────────────────────────────

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

static VkShaderModule vmMakeMod(VkDevice dev, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * 4;
    ci.pCode    = code.data();
    VkShaderModule m;
    vmCheck(vkCreateShaderModule(dev, &ci, nullptr, &m), "viewmodel shader module");
    return m;
}

static void uploadBuf(VkDevice device, VmaAllocator allocator,
                      VkCommandPool pool, VkQueue queue,
                      const void* data, VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkBuffer& outBuf, VmaAllocation& outAlloc) {
    VkBuffer stg; VmaAllocation sa;
    {
        VkBufferCreateInfo b{}; b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = size; b.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo a{}; a.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        a.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        vmaCreateBuffer(allocator, &b, &a, &stg, &sa, &info);
        memcpy(info.pMappedData, data, size);
    }
    {
        VkBufferCreateInfo b{}; b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        b.size = size; b.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo a{}; a.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(allocator, &b, &a, &outBuf, &outAlloc, nullptr);
    }
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy r{0, 0, size};
    vkCmdCopyBuffer(cmd, stg, outBuf, 1, &r);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    vmaDestroyBuffer(allocator, stg, sa);
}

// ── init ──────────────────────────────────────────────────────────────────────

void ViewModelRenderer::init(VkDevice device, VmaAllocator /*allocator*/,
                              VkRenderPass renderPass, VkExtent2D /*extent*/,
                              const char* vertSpv, const char* fragSpv) {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo li{};
    li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges    = &pcr;
    vmCheck(vkCreatePipelineLayout(device, &li, nullptr, &pipelineLayout),
            "viewmodel pipeline layout");

    auto vc = vmLoadSpv(vertSpv), fc = vmLoadSpv(fragSpv);
    VkShaderModule vm = vmMakeMod(device, vc), fm = vmMakeMod(device, fc);

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT,   vm, "main"};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main"};

    VkVertexInputBindingDescription binding{0, sizeof(GltfVertex),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GltfVertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;   vi.pVertexBindingDescriptions   = &binding;
    vi.vertexAttributeDescriptionCount = 3;   vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynCI{};
    dynCI.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynCI.dynamicStateCount = 2; dynCI.pDynamicStates = dyn;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // CULL_MODE_NONE: mirrored offhand faces must still render
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo bl{};
    bl.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    bl.attachmentCount = 1; bl.pAttachments = &ba;

    VkGraphicsPipelineCreateInfo pCI{};
    pCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pCI.stageCount          = 2;   pCI.pStages            = st;
    pCI.pVertexInputState   = &vi; pCI.pInputAssemblyState = &ia;
    pCI.pViewportState      = &vps;pCI.pRasterizationState = &rs;
    pCI.pMultisampleState   = &ms; pCI.pDepthStencilState  = &ds;
    pCI.pColorBlendState    = &bl; pCI.pDynamicState       = &dynCI;
    pCI.layout              = pipelineLayout;
    pCI.renderPass          = renderPass;
    vmCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pCI, nullptr, &pipeline),
            "viewmodel pipeline");

    vkDestroyShaderModule(device, vm, nullptr);
    vkDestroyShaderModule(device, fm, nullptr);

    // Offhand uses idle-only animation by default
    offhandAnim.clips[(int)AnimSlot::Idle]        = makeDefaultIdle();
    offhandAnim.clips[(int)AnimSlot::LightAttack] = makeDefaultIdle();
    offhandAnim.clips[(int)AnimSlot::HeavyAttack] = makeDefaultIdle();
    offhandAnim.play(AnimSlot::Idle);

    Log::info("ViewModelRenderer initialised");
}

// ── loadMesh ──────────────────────────────────────────────────────────────────

int ViewModelRenderer::loadMesh(VkDevice device, VmaAllocator allocator,
                                 VkCommandPool pool, VkQueue queue,
                                 const GltfModel& model,
                                 ViewModelTransform transform) {
    if (!model.valid || model.meshes.empty()) return -1;

    std::vector<GltfVertex> verts;
    std::vector<uint32_t>   inds;
    for (auto& m : model.meshes) {
        uint32_t base = (uint32_t)verts.size();
        verts.insert(verts.end(), m.vertices.begin(), m.vertices.end());
        for (auto i : m.indices) inds.push_back(base + i);
    }

    // Auto-compute mesh centre for pivot correction
    if (!verts.empty()) {
        glm::vec3 mn = verts[0].pos, mx = verts[0].pos;
        for (auto& v : verts) { mn = glm::min(mn, v.pos); mx = glm::max(mx, v.pos); }
        transform.meshCenter = (mn + mx) * 0.5f;
    }

    ViewModelMesh gpu{};
    gpu.indexCount = (uint32_t)inds.size();
    uploadBuf(device, allocator, pool, queue,
              verts.data(), verts.size() * sizeof(GltfVertex),
              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu.vertBuf, gpu.vertAlloc);
    uploadBuf(device, allocator, pool, queue,
              inds.data(), inds.size() * sizeof(uint32_t),
              VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gpu.idxBuf, gpu.idxAlloc);

    meshes.push_back(gpu);
    transforms.push_back(transform);
    return (int)meshes.size() - 1;
}

// ── registerItemMesh ──────────────────────────────────────────────────────────

void ViewModelRenderer::registerItemMesh(ItemID id, int meshIdx,
                                          ViewModelTransform mainTransform,
                                          ViewModelTransform offhandTransform) {
    // If no offhand transform provided (all zeros scale), derive from main
    bool offhandDefault = (offhandTransform.scale.x == 0.f &&
                           offhandTransform.scale.y == 0.f &&
                           offhandTransform.scale.z == 0.f);
    if (offhandDefault) {
        offhandTransform = mainTransform;
        // Mirror: flip X offset and X scale so it sits in the left hand
        offhandTransform.offset.x = -mainTransform.offset.x;
        offhandTransform.scale.x  = -mainTransform.scale.x;
        offhandTransform.rotation.y = -mainTransform.rotation.y;
        offhandTransform.rotation.z = -mainTransform.rotation.z;
    }
    // Copy meshCenter from the loaded mesh
    if (meshIdx >= 0 && meshIdx < (int)meshes.size()) {
        mainTransform.meshCenter    = transforms[meshIdx].meshCenter;
        offhandTransform.meshCenter = transforms[meshIdx].meshCenter;
    }
    itemRegistry[(int)id] = { meshIdx, mainTransform, offhandTransform };
    Log::info("Registered item view model: id=" + std::to_string((int)id)
              + " meshIdx=" + std::to_string(meshIdx));
}

// ── syncEquipped ──────────────────────────────────────────────────────────────

void ViewModelRenderer::syncEquipped(ItemID mainHandId, ItemID offhandId) {
    // Main hand
    if (mainHandId != lastMainHandId) {
        lastMainHandId = mainHandId;
        auto it = itemRegistry.find((int)mainHandId);
        if (it != itemRegistry.end()) {
            activeMeshIdx       = it->second.meshIdx;
            activeMainTransform = it->second.mainTransform;
        } else {
            activeMeshIdx = -1; // fists / nothing
        }
        // Reset animation when item changes
        anim.play(AnimSlot::Idle);
    }

    // Offhand
    if (offhandId != lastOffhandId) {
        lastOffhandId = offhandId;
        auto it = itemRegistry.find((int)offhandId);
        if (it != itemRegistry.end()) {
            offhandMeshIdx          = it->second.meshIdx;
            activeOffhandTransform  = it->second.offhandTransform;
        } else {
            offhandMeshIdx = -1;
        }
        offhandAnim.play(AnimSlot::Idle);
    }
}

// ── drawMesh (internal) ───────────────────────────────────────────────────────

void ViewModelRenderer::drawMesh(VkCommandBuffer cmd, const glm::mat4& proj,
                                  int meshIdx, const ViewModelTransform& t,
                                  const AnimationPlayer& ap, bool mirrorX) const {
    if (meshIdx < 0 || meshIdx >= (int)meshes.size()) return;
    const auto& mesh = meshes[meshIdx];
    if (!mesh.vertBuf || !mesh.idxBuf) return;

    glm::vec3 animOff, animRot, animScl;
    ap.getCurrentDelta(animOff, animRot, animScl);

    glm::vec3 offset = t.offset   + animOff;
    glm::vec3 scale  = t.scale    * animScl;
    glm::vec3 rot    = t.rotation + animRot;

    if (mirrorX) {
        // Mirror across the view's YZ plane for left-hand placement.
        // Negative scale flips winding; CULL_MODE_NONE handles that.
        offset.x = -offset.x;
        scale.x  = -scale.x;
        rot.y    = -rot.y;
        rot.z    = -rot.z;
    }

    glm::mat4 model = glm::mat4(1.f);
    model = glm::translate(model, offset);
    model = model * glm::eulerAngleXYZ(glm::radians(rot.x),
                                       glm::radians(rot.y),
                                       glm::radians(rot.z));
    model = glm::scale(model, scale);
    model = glm::translate(model, -t.meshCenter);

    glm::mat4 mvp = proj * model;

    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertBuf, &zero);
    vkCmdBindIndexBuffer(cmd, mesh.idxBuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::mat4), &mvp);
    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}

// ── draw ──────────────────────────────────────────────────────────────────────

void ViewModelRenderer::draw(VkCommandBuffer cmd, const glm::mat4& proj,
                              VkExtent2D extent) const {
    if (armMeshIdx < 0 && activeMeshIdx < 0 && offhandMeshIdx < 0) return;

    VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0.f, 1.f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Arm (always right hand, uses arm's stored transform, no anim mirror)
    if (armMeshIdx >= 0 && armMeshIdx < (int)transforms.size())
        drawMesh(cmd, proj, armMeshIdx, transforms[armMeshIdx], anim, false);

    // Main hand item (right, no mirror)
    if (activeMeshIdx >= 0)
        drawMesh(cmd, proj, activeMeshIdx, activeMainTransform, anim, false);

    // Offhand item (left, mirrored)
    if (offhandMeshIdx >= 0)
        drawMesh(cmd, proj, offhandMeshIdx, activeOffhandTransform, offhandAnim, true);
}

// ── debug UI ─────────────────────────────────────────────────────────────────

void ViewModelRenderer::drawTransformEditor(ViewModelTransform& t,
                                             const AnimationPlayer& ap,
                                             ItemID itemId, bool isOffhand) {
    ImGui::DragFloat3("Offset",   &t.offset.x,   0.005f, -5.f,   5.f);
    ImGui::DragFloat3("Rotation", &t.rotation.x, 0.5f,  -360.f, 360.f);
    ImGui::DragFloat3("Scale",    &t.scale.x,    0.0005f, -2.f,   2.f);
    ImGui::Separator();

    glm::vec3 aOff, aRot, aScl;
    ap.getCurrentDelta(aOff, aRot, aScl);
    ImGui::TextColored({0.5f,0.8f,1.f,1.f},
                       "Anim delta  Off(%.3f %.3f %.3f)  Rot(%.1f %.1f %.1f)",
                       aOff.x, aOff.y, aOff.z, aRot.x, aRot.y, aRot.z);
    ImGui::Separator();

    // Show which item this is for
    const char* slot = isOffhand ? "offhand" : "main";
    const char* itemName = getItemDef(itemId).name.data();
    ImGui::TextDisabled("Item: %s  |  Slot: %s  |  meshCenter: %.2f %.2f %.2f",
                        itemName, slot,
                        t.meshCenter.x, t.meshCenter.y, t.meshCenter.z);

    // Copy-paste block
    ImGui::Separator();
    char buf[400];
    snprintf(buf, sizeof(buf),
             "// ItemID::%s  (%s hand)\n"
             "t.offset   = {%.4ff, %.4ff, %.4ff};\n"
             "t.rotation = {%.1ff, %.1ff, %.1ff};\n"
             "t.scale    = {%.5ff, %.5ff, %.5ff};",
             itemName, slot,
             t.offset.x,   t.offset.y,   t.offset.z,
             t.rotation.x, t.rotation.y, t.rotation.z,
             t.scale.x,    t.scale.y,    t.scale.z);
    ImGui::InputTextMultiline("##copy", buf, sizeof(buf),
                              {-1.f, 90.f}, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button("Copy##xform")) ImGui::SetClipboardText(buf);

    // Also write back to the registry so it persists if you swap items and back
    auto it = itemRegistry.find((int)itemId);
    if (it != itemRegistry.end()) {
        if (isOffhand) it->second.offhandTransform = t;
        else           it->second.mainTransform    = t;
    }
}

void ViewModelRenderer::drawDebugUI() {
    if (!uiVisible) return;

    ImGui::SetNextWindowSize({370, 320}, ImGuiCond_Once);
    ImGui::SetNextWindowPos({10, 540},   ImGuiCond_Always);
    bool windowOpen = true;
    ImGui::Begin("Viewmodel Transform", &windowOpen, ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("##handtabs")) {
        if (ImGui::BeginTabItem("Main Hand")) {
            debugSlot = DebugHandSlot::MainHand;
            if (activeMeshIdx >= 0)
                drawTransformEditor(activeMainTransform, anim,
                                    lastMainHandId, false);
            else if (armMeshIdx >= 0)
                drawTransformEditor(transforms[armMeshIdx], anim,
                                    ItemID::None, false);
            else
                ImGui::TextDisabled("Nothing equipped.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Off Hand")) {
            debugSlot = DebugHandSlot::OffHand;
            if (offhandMeshIdx >= 0)
                drawTransformEditor(activeOffhandTransform, offhandAnim,
                                    lastOffhandId, true);
            else
                ImGui::TextDisabled("Nothing in offhand.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Arm")) {
            debugSlot = DebugHandSlot::MainHand;
            if (armMeshIdx >= 0 && armMeshIdx < (int)transforms.size())
                drawTransformEditor(transforms[armMeshIdx], anim,
                                    ItemID::None, false);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    if (!windowOpen) {
        uiVisible     = false;
        animEditor.open = false;
        return;
    }

    animEditor.open = true;
    animEditor.draw(anim);
}

// ── destroy ───────────────────────────────────────────────────────────────────

void ViewModelRenderer::destroy(VkDevice device, VmaAllocator allocator) {
    for (auto& m : meshes) {
        vmaDestroyBuffer(allocator, m.vertBuf, m.vertAlloc);
        vmaDestroyBuffer(allocator, m.idxBuf,  m.idxAlloc);
    }
    meshes.clear();
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
}
