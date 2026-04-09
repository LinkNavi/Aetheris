#pragma once
#include <imgui.h>

// Lightweight in-game pause menu.
// Call draw() every frame while InGame. Returns PauseAction each frame.
enum class PauseAction {
    None,
    Resume,
    BackToMainMenu,
    QuitGame,
};

class PauseMenu {
public:
    bool visible = false;

    void toggle() { visible = !visible; }

    // Returns action taken this frame (None if nothing clicked).
    PauseAction draw() {
        if (!visible) return PauseAction::None;

        ImGuiIO& io = ImGui::GetIO();
        float sw = io.DisplaySize.x;
        float sh = io.DisplaySize.y;

        // Dim background
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            {0, 0}, {sw, sh}, IM_COL32(0, 0, 0, 160));

        float panW = 300.f, panH = 280.f;
        float px = (sw - panW) * 0.5f, py = (sh - panH) * 0.5f;

        ImGui::SetNextWindowPos({px, py}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({panW, panH}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 0.f});
        ImGui::Begin("##pausemenu", nullptr,
            ImGuiWindowFlags_NoDecoration  |
            ImGuiWindowFlags_NoMove        |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar(2);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImFont* font = ImGui::GetFont();

        // Title bar
        dl->AddRectFilled(wp, {wp.x + panW, wp.y + 44.f}, IM_COL32(8, 10, 18, 255));
        dl->AddLine({wp.x, wp.y + 44.f}, {wp.x + panW, wp.y + 44.f},
                    IM_COL32(40, 100, 160, 200), 1.f);
        const char* title = "PAUSED";
        ImVec2 tsz = font->CalcTextSizeA(22.f, 9999.f, 0.f, title);
        dl->AddText(font, 22.f, {wp.x + (panW - tsz.x) * 0.5f, wp.y + 11.f},
                    IM_COL32(180, 210, 255, 255), title);

        PauseAction action = PauseAction::None;

        float btnW = panW - 48.f, btnH = 42.f;
        float bx = wp.x + 24.f;
        float by = wp.y + 60.f;
        float gap = 12.f;

        struct Btn { const char* label; PauseAction act; ImU32 col; ImU32 hov; };
        static const Btn BTNS[] = {
            {"Resume",           PauseAction::Resume,        IM_COL32(20,60,20,220),  IM_COL32(30,90,30,230)},
            {"Main Menu",        PauseAction::BackToMainMenu, IM_COL32(10,30,60,220), IM_COL32(15,50,90,230)},
            {"Quit to Desktop",  PauseAction::QuitGame,       IM_COL32(60,15,15,220), IM_COL32(90,20,20,230)},
        };

        for (auto& b : BTNS) {
            ImGui::SetCursorScreenPos({bx, by});
            ImGui::PushID(b.label);
            bool hov = false;
            ImGui::InvisibleButton("##pbtn", {btnW, btnH});
            hov = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked();
            ImGui::PopID();

            dl->AddRectFilled({bx, by}, {bx + btnW, by + btnH},
                              hov ? b.hov : b.col, 4.f);
            dl->AddRect({bx, by}, {bx + btnW, by + btnH},
                        IM_COL32(60, 120, 200, hov ? 200u : 80u), 4.f, 0, 1.f);

            ImVec2 lsz = font->CalcTextSizeA(17.f, 9999.f, 0.f, b.label);
            dl->AddText(font, 17.f,
                        {bx + (btnW - lsz.x) * 0.5f, by + (btnH - lsz.y) * 0.5f},
                        IM_COL32(210, 225, 255, 255), b.label);

            if (clicked) action = b.act;
            by += btnH + gap;
        }

        ImGui::End();
        return action;
    }
};
