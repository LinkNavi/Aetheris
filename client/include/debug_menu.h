#pragma once
#include <imgui.h>
#include <glm/vec3.hpp>
#include <enet/enet.h>
#include "packets.h"
#include "net_common.h"
#include <cmath>
// ── DebugMenu ─────────────────────────────────────────────────────────────────
// Press F3 to toggle. Shows player position, chunk coords,
// and buttons to spawn trees / water sources at current position.

class DebugMenu {
public:
    bool visible = false;

    void toggle() { visible = !visible; }

    // Call inside ImGui frame. Returns true if any action was taken.
    // pos = player world position, server = ENet peer to send debug packets to
    bool draw(glm::vec3 pos, ENetPeer* server) {
        if (!visible) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({io.DisplaySize.x - 320.f, 10.f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({310.f, 0.f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("Debug", &visible,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize   |
            ImGuiWindowFlags_NoMove     |
            ImGuiWindowFlags_NoSavedSettings);

        // ── Position info ─────────────────────────────────────────────────
        ImGui::TextColored({0.4f, 0.8f, 1.f, 1.f}, "Position");
        ImGui::Text("X: %.2f  Y: %.2f  Z: %.2f", pos.x, pos.y, pos.z);

        int chunkX = (int)std::floor(pos.x / (float)ChunkData::SIZE);
        int chunkY = (int)std::floor(pos.y / (float)ChunkData::SIZE);
        int chunkZ = (int)std::floor(pos.z / (float)ChunkData::SIZE);
        ImGui::Text("Chunk: %d, %d, %d", chunkX, chunkY, chunkZ);

        int blockX = (int)std::floor(pos.x);
        int blockY = (int)std::floor(pos.y);
        int blockZ = (int)std::floor(pos.z);
        ImGui::Text("Block:  %d, %d, %d", blockX, blockY, blockZ);

        ImGui::Separator();

        // ── Spawn controls ────────────────────────────────────────────────
        ImGui::TextColored({0.4f, 0.8f, 1.f, 1.f}, "Spawn at current position");

        bool acted = false;

        // Spawn random tree
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.35f, 0.15f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.50f, 0.20f, 1.f});
        if (ImGui::Button("Spawn Tree", {140.f, 30.f})) {
            if (server) {
                SpawnTreePacket pkt{pos.x, pos.y, pos.z};
                Net::sendReliable(server, pkt.serialize());
                acted = true;
            }
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spawns a random tree at your feet");

        ImGui::SameLine();

        // Place water source
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.10f, 0.20f, 0.45f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.15f, 0.30f, 0.60f, 1.f});
        if (ImGui::Button("Water Source", {140.f, 30.f})) {
            if (server) {
                WaterPlacePacket pkt{blockX, blockY, blockZ, 8};
                Net::sendReliable(server, pkt.serialize());
                acted = true;
            }
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Places a water source block at your feet\nWater will flow from here");

        ImGui::Separator();

        // ── Water removal ─────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.35f, 0.10f, 0.10f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.50f, 0.15f, 0.15f, 1.f});
        if (ImGui::Button("Remove Water Here", {295.f, 26.f})) {
            if (server) {
                WaterPlacePacket pkt{blockX, blockY, blockZ, 0};
                Net::sendReliable(server, pkt.serialize());
                acted = true;
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::Separator();

        // ── Flood fill ────────────────────────────────────────────────────
        ImGui::TextColored({0.7f, 0.5f, 0.1f, 1.f}, "Danger Zone");
        static int floodRadius = 4;
        ImGui::SliderInt("Flood radius", &floodRadius, 1, 16);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.45f, 0.30f, 0.05f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.60f, 0.40f, 0.08f, 1.f});
        if (ImGui::Button("Flood Area", {295.f, 26.f})) {
            if (server) {
                // Place water sources in a radius
                for (int dx = -floodRadius; dx <= floodRadius; dx++)
                for (int dz = -floodRadius; dz <= floodRadius; dz++) {
                    if (dx*dx + dz*dz <= floodRadius*floodRadius) {
                        WaterPlacePacket pkt{
                            blockX + dx,
                            blockY,
                            blockZ + dz,
                            8
                        };
                        Net::sendReliable(server, pkt.serialize());
                    }
                }
                acted = true;
            }
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Places water sources in a radius around you");

        ImGui::End();
        return acted;
    }
};
