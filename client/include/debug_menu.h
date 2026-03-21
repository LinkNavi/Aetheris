#pragma once
#include <imgui.h>
#include <glm/vec3.hpp>
#include <enet/enet.h>
#include "packets.h"
#include "net_common.h"
#include "day_night.h"
#include <cmath>

class DebugMenu {
public:
    bool visible = false;
    bool timeOverride = false; // when true, client ignores server time packets

    void toggle() { visible = !visible; }

    bool draw(glm::vec3 pos, ENetPeer* server, DayNight& dayNight) {
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

        // ── Time control ──────────────────────────────────────────────────
        ImGui::TextColored({0.4f, 0.8f, 1.f, 1.f}, "Time of Day");

        // Convert time [0,1] to hours for display
        float hours = dayNight.time * 24.f;
        int   h     = (int)hours;
        int   m     = (int)((hours - h) * 60.f);
        ImGui::Text("Current: %02d:%02d  (%s)",
            h, m,
            hours < 6.f  ? "Night"   :
            hours < 8.f  ? "Dawn"    :
            hours < 17.f ? "Day"     :
            hours < 19.f ? "Dusk"    : "Night");

        ImGui::Checkbox("Override server time", &timeOverride);

        if (timeOverride) {
            ImGui::PushStyleColor(ImGuiCol_SliderGrab,     {0.9f, 0.7f, 0.2f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,{1.f, 0.8f, 0.3f, 1.f});
            ImGui::SliderFloat("##timeslider", &dayNight.time, 0.f, 1.f, "");
            ImGui::PopStyleColor(2);

            // Quick preset buttons
            ImGui::PushStyleColor(ImGuiCol_Button, {0.08f, 0.08f, 0.15f, 1.f});
            if (ImGui::Button("Dawn",     {60.f, 22.f})) dayNight.time = 6.f  / 24.f;
            ImGui::SameLine();
            if (ImGui::Button("Noon",     {60.f, 22.f})) dayNight.time = 12.f / 24.f;
            ImGui::SameLine();
            if (ImGui::Button("Dusk",     {60.f, 22.f})) dayNight.time = 18.f / 24.f;
            ImGui::SameLine();
            if (ImGui::Button("Midnight", {70.f, 22.f})) dayNight.time = 0.f;
            ImGui::PopStyleColor();

            // Send override to server so other players see it too
            if (ImGui::Button("Sync to Server", {295.f, 26.f})) {
                if (server) {
                    WorldTimePacket pkt{dayNight.time};
                    Net::sendReliable(server, pkt.serialize());
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Push your current time to the server\nso all other players sync to it");
        }

        ImGui::Separator();

        // ── Spawn controls ────────────────────────────────────────────────
        ImGui::TextColored({0.4f, 0.8f, 1.f, 1.f}, "Spawn at current position");

        bool acted = false;

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
            ImGui::SetTooltip("Places a water source block at your feet");

        ImGui::Separator();

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

        ImGui::TextColored({0.7f, 0.5f, 0.1f, 1.f}, "Danger Zone");
        static int floodRadius = 4;
        ImGui::SliderInt("Flood radius", &floodRadius, 1, 16);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.45f, 0.30f, 0.05f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.60f, 0.40f, 0.08f, 1.f});
        if (ImGui::Button("Flood Area", {295.f, 26.f})) {
            if (server) {
                for (int dx = -floodRadius; dx <= floodRadius; dx++)
                for (int dz = -floodRadius; dz <= floodRadius; dz++) {
                    if (dx*dx + dz*dz <= floodRadius*floodRadius) {
                        WaterPlacePacket pkt{blockX+dx, blockY, blockZ+dz, 8};
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
