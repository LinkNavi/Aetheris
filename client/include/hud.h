#pragma once
#include <imgui.h>
#include "player_stats.h"

// Client-side mirror of server stats — updated by StatsSyncPacket / StatsDeltaPacket
struct ClientStats {
    float health    = 100.f, healthMax  = 100.f;
    float stamina   = 100.f, staminaMax = 100.f;
    float mana      = 100.f, manaMax    = 100.f;
    float armour    = 0.f,   armourMax  = 100.f;
    bool  dead      = false;

    void applySync(const StatsSyncPacket& p) {
        health = p.health; healthMax = p.healthMax;
        stamina = p.stamina; staminaMax = p.staminaMax;
        mana = p.mana; manaMax = p.manaMax;
        armour = p.armour; armourMax = p.armourMax;
        dead = p.dead != 0;
    }

    void applyDelta(const StatsDeltaPacket& p) {
        health = p.health; stamina = p.stamina;
        mana = p.mana; armour = p.armour;
        dead = p.dead != 0;
    }
};

// Draws the HUD bars. Call inside ImGui frame every frame.
class HUD {
public:
    void draw(const ClientStats& stats) {
        ImGuiIO& io = ImGui::GetIO();

        // Position: bottom-left, above hotbar
        float hudW = 260.f;
        float hudH = 110.f;
        float x = 12.f;
        float y = io.DisplaySize.y - hudH - 80.f; // above hotbar

        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({hudW, hudH}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("##hud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        float pad = 8.f;
        float barW = hudW - pad * 2;
        float barH = 16.f;
        float gap  = 4.f;
        float cx = wp.x + pad;
        float cy = wp.y + pad;

        // Health
        drawBar(dl, cx, cy, barW, barH, stats.health, stats.healthMax,
                IM_COL32(180, 30, 30, 255), IM_COL32(60, 10, 10, 200), "HP");
        cy += barH + gap;

        // Stamina
        drawBar(dl, cx, cy, barW, barH, stats.stamina, stats.staminaMax,
                IM_COL32(40, 180, 40, 255), IM_COL32(10, 50, 10, 200), "STA");
        cy += barH + gap;

        // Mana
        drawBar(dl, cx, cy, barW, barH, stats.mana, stats.manaMax,
                IM_COL32(40, 80, 200, 255), IM_COL32(10, 20, 60, 200), "MP");
        cy += barH + gap;

        // Armour
        drawBar(dl, cx, cy, barW, barH, stats.armour, stats.armourMax,
                IM_COL32(160, 160, 170, 255), IM_COL32(40, 40, 50, 200), "ARM");

        // Dead overlay
        if (stats.dead) {
            ImVec2 center = {io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f};
            const char* txt = "YOU DIED  -  Press R to respawn";
            ImVec2 sz = ImGui::CalcTextSize(txt);
            dl->AddRectFilled({center.x - sz.x * 0.5f - 20, center.y - 20},
                              {center.x + sz.x * 0.5f + 20, center.y + 30},
                              IM_COL32(0, 0, 0, 180), 6.f);
            dl->AddText({center.x - sz.x * 0.5f, center.y - 8},
                        IM_COL32(220, 30, 30, 255), txt);
        }

        ImGui::End();
    }

private:
    void drawBar(ImDrawList* dl, float x, float y, float w, float h,
                 float val, float max, ImU32 fillCol, ImU32 bgCol, const char* label) {
        float frac = (max > 0.f) ? val / max : 0.f;
        if (frac < 0.f) frac = 0.f;
        if (frac > 1.f) frac = 1.f;

        // Background
        dl->AddRectFilled({x, y}, {x + w, y + h}, bgCol, 3.f);
        // Fill
        if (frac > 0.001f)
            dl->AddRectFilled({x, y}, {x + w * frac, y + h}, fillCol, 3.f);
        // Border
        dl->AddRect({x, y}, {x + w, y + h}, IM_COL32(200, 200, 200, 100), 3.f);

        // Label + value
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %.0f/%.0f", label, val, max);
        ImVec2 tsz = ImGui::CalcTextSize(buf);
        dl->AddText({x + (w - tsz.x) * 0.5f, y + (h - tsz.y) * 0.5f},
                    IM_COL32(255, 255, 255, 230), buf);
    }
};
