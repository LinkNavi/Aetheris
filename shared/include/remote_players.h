#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <string>
#include <imgui.h>
#include "gltf_loader.h"
#include "mp_packets.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

struct RemotePlayerState {
    uint32_t    id = 0;
    std::string username;
    glm::vec3   pos{0.f};
    glm::vec3   prevPos{0.f};   // for interpolation
    glm::vec3   renderPos{0.f}; // interpolated
    float       yaw   = 0.f;
    float       pitch = 0.f;
    float       interpT = 0.f;
    bool        active = false;
};

struct PlayerModelGPU {
    VkBuffer      vertBuf   = VK_NULL_HANDLE;
    VmaAllocation vertAlloc = nullptr;
    VkBuffer      idxBuf    = VK_NULL_HANDLE;
    VmaAllocation idxAlloc  = nullptr;
    uint32_t      indexCount = 0;
    bool          loaded = false;
};

// Manages rendering of all remote (other) players.
// Uses the same viewmodel pipeline (pos/normal/uv vertex layout).
class RemotePlayerRenderer {
public:
    std::unordered_map<uint32_t, RemotePlayerState> players;
    PlayerModelGPU                                  model;
    uint32_t localPlayerId = 0;

    void onSpawn(const PlayerSpawnPacket& pkt) {
        if (pkt.playerId == localPlayerId) return;
        auto& p     = players[pkt.playerId];
        p.id        = pkt.playerId;
        p.username  = pkt.username;
        p.pos       = {pkt.x, pkt.y, pkt.z};
        p.prevPos   = p.pos;
        p.renderPos = p.pos;
        p.yaw       = pkt.yaw;
        p.active    = true;
    }

    void onDespawn(uint32_t playerId) {
        players.erase(playerId);
    }

    void onPosSync(const PlayerPosSyncPacket& pkt) {
        for (const auto& entry : pkt.players) {
            if (entry.playerId == localPlayerId) continue;
            auto it = players.find(entry.playerId);
            if (it == players.end()) continue;
            auto& p   = it->second;
            p.prevPos  = p.renderPos;
            p.pos      = {entry.x, entry.y, entry.z};
            p.yaw      = entry.yaw;
            p.pitch    = entry.pitch;
            p.interpT  = 0.f;
        }
    }

    void update(float dt) {
        // Interpolate toward latest server position
        constexpr float INTERP_SPEED = 10.f; // ~100ms to reach target at 20Hz
        for (auto& [id, p] : players) {
            p.interpT = std::min(p.interpT + dt * INTERP_SPEED, 1.f);
            p.renderPos = glm::mix(p.prevPos, p.pos, p.interpT);
        }
    }

    bool loadModel(VkDevice device, VmaAllocator allocator,
                   VkCommandPool pool, VkQueue queue,
                   const char* glbPath) {
        GltfModel gltf = loadGlb(glbPath);
        if (!gltf.valid || gltf.meshes.empty()) return false;

        std::vector<GltfVertex> verts;
        std::vector<uint32_t>   inds;
        for (auto& m : gltf.meshes) {
            uint32_t base = (uint32_t)verts.size();
            verts.insert(verts.end(), m.vertices.begin(), m.vertices.end());
            for (auto i : m.indices) inds.push_back(base + i);
        }

        model.indexCount = (uint32_t)inds.size();
        uploadBuf(device, allocator, pool, queue,
                  verts.data(), verts.size() * sizeof(GltfVertex),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  model.vertBuf, model.vertAlloc);
        uploadBuf(device, allocator, pool, queue,
                  inds.data(), inds.size() * sizeof(uint32_t),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  model.idxBuf, model.idxAlloc);
        model.loaded = true;
        return true;
    }

    // Draw all remote players. Call inside render pass after terrain + viewmodel.
    // Uses the viewmodel pipeline (same vertex layout: pos/normal/uv, push constant MVP).
    void draw(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
              const glm::mat4& viewProj) const {
        if (!model.loaded || players.empty()) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &model.vertBuf, &zero);
        vkCmdBindIndexBuffer(cmd, model.idxBuf, 0, VK_INDEX_TYPE_UINT32);

        for (const auto& [id, p] : players) {
            if (!p.active) continue;

            glm::mat4 m = glm::mat4(1.f);
            m = glm::translate(m, p.renderPos);
            m = glm::rotate(m, glm::radians(-p.yaw + 90.f), {0, 1, 0});
            m = glm::scale(m, glm::vec3(0.6f)); // scale to roughly player size

            glm::mat4 mvp = viewProj * m;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(glm::mat4), &mvp);
            vkCmdDrawIndexed(cmd, model.indexCount, 1, 0, 0, 0);
        }
    }

    // Draw nametags via ImGui overlay
    void drawNametags(const glm::mat4& viewProj, int screenW, int screenH) const {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        for (const auto& [id, p] : players) {
            if (!p.active) continue;
            glm::vec3 headPos = p.renderPos + glm::vec3(0, 2.2f, 0);
            glm::vec4 clip = viewProj * glm::vec4(headPos, 1.f);
            if (clip.w <= 0.01f) continue;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            float sx = (ndc.x * 0.5f + 0.5f) * screenW;
            float sy = (1.f - (ndc.y * 0.5f + 0.5f)) * screenH;

            if (sx < -100 || sx > screenW + 100 || sy < -50 || sy > screenH + 50) continue;

            const char* name = p.username.c_str();
            ImVec2 tsz = ImGui::CalcTextSize(name);
            float tx = sx - tsz.x * 0.5f;
            float ty = sy - tsz.y;

            // Background
            dl->AddRectFilled({tx - 4, ty - 2}, {tx + tsz.x + 4, ty + tsz.y + 2},
                              IM_COL32(0, 0, 0, 140), 3.f);
            dl->AddText({tx, ty}, IM_COL32(220, 230, 240, 255), name);
        }
    }

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (model.vertBuf) vmaDestroyBuffer(allocator, model.vertBuf, model.vertAlloc);
        if (model.idxBuf)  vmaDestroyBuffer(allocator, model.idxBuf, model.idxAlloc);
        model = {};
    }

private:
    static void uploadBuf(VkDevice device, VmaAllocator allocator,
                          VkCommandPool pool, VkQueue queue,
                          const void* data, VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkBuffer& outBuf, VmaAllocation& outAlloc) {
        VkBuffer stageBuf; VmaAllocation stageAlloc;
        {
            VkBufferCreateInfo bCI{}; bCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bCI.size = size; bCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo aCI{}; aCI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            aCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo info{};
            vmaCreateBuffer(allocator, &bCI, &aCI, &stageBuf, &stageAlloc, &info);
            memcpy(info.pMappedData, data, size);
        }
        {
            VkBufferCreateInfo bCI{}; bCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bCI.size = size; bCI.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo aCI{}; aCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            vmaCreateBuffer(allocator, &bCI, &aCI, &outBuf, &outAlloc, nullptr);
        }
        VkCommandBufferAllocateInfo cbAI{}; cbAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAI.commandPool = pool; cbAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbAI.commandBufferCount = 1;
        VkCommandBuffer cmd; vkAllocateCommandBuffers(device, &cbAI, &cmd);
        VkCommandBufferBeginInfo bI{}; bI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bI);
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, stageBuf, outBuf, 1, &region);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        vmaDestroyBuffer(allocator, stageBuf, stageAlloc);
    }
};
