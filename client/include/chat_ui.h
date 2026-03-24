#pragma once
#include <imgui.h>
#include <enet/enet.h>
#include <string>
#include <vector>
#include <deque>
#include <cstring>
#include "chat_packets.h"
#include "net_common.h"

struct ChatEntry {
    std::string username; // empty = system message
    std::string text;
    float       timestamp = 0.f; // seconds since startup, for fade
};

class ChatUI {
public:
    static constexpr int   MAX_HISTORY   = 100;
    static constexpr float FADE_AFTER    = 8.f;  // seconds before messages fade
    static constexpr float FADE_DURATION = 2.f;
    static constexpr float INPUT_MAX     = 256;

    // Call every frame. Returns true if chat captured keyboard input this frame
    // (so the game should suppress movement keys etc.)
    bool draw(float dt, float appTime, ENetPeer* server) {
        _appTime = appTime;
        _dt      = dt;

        drawMessages(appTime);

        if (_inputOpen)
            return drawInput(server);

        return false;
    }

    // Call on key press in the main loop BEFORE ImGui steals input.
    // Returns true if the key was consumed by chat.
    bool onKeyDown(int glfwKey) {
        if (glfwKey == GLFW_KEY_ENTER || glfwKey == GLFW_KEY_KP_ENTER) {
            if (!_inputOpen) {
                openInput();
                return true;
            }
            // Input is open — submit handled inside drawInput via ImGui
        }
        if (glfwKey == GLFW_KEY_ESCAPE && _inputOpen) {
            closeInput();
            return true;
        }
        return false;
    }

    bool isOpen() const { return _inputOpen; }

    // Push a received broadcast into history
    void pushMessage(const std::string& username, const std::string& text) {
        if (_history.size() >= MAX_HISTORY) _history.pop_front();
        _history.push_back({username, text, _appTime});
        _scrollToBottom = true;
    }

    // Push a system/local message (no username)
    void pushSystem(const std::string& text) {
        pushMessage("", text);
    }

private:
    std::deque<ChatEntry> _history;
    char   _inputBuf[257] = {};
    bool   _inputOpen      = false;
    bool   _scrollToBottom = false;
    bool   _focusNext      = false;
    float  _appTime        = 0.f;
    float  _dt             = 0.f;

    void openInput() {
        _inputOpen  = true;
        _focusNext  = true;
        memset(_inputBuf, 0, sizeof(_inputBuf));
    }

    void closeInput() {
        _inputOpen = false;
        memset(_inputBuf, 0, sizeof(_inputBuf));
    }

    // Passive message display — bottom-left, fades old messages when input closed
    void drawMessages(float appTime) {
        ImGuiIO& io = ImGui::GetIO();

        float winW  = 460.f;
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        int   visibleLines = 12;
        float winH  = lineH * visibleLines + 8.f;
        float x     = 10.f;
        float y     = io.DisplaySize.y - winH - (_inputOpen ? 38.f : 10.f);

        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({winW, winH}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(_inputOpen ? 0.55f : 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.f, 4.f});
        ImGui::Begin("##chatlog", nullptr,
            ImGuiWindowFlags_NoDecoration   |
            ImGuiWindowFlags_NoNav          |
            ImGuiWindowFlags_NoMove         |
            ImGuiWindowFlags_NoSavedSettings|
            ImGuiWindowFlags_NoInputs       |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Show last N messages
        int start = (int)_history.size() - visibleLines;
        if (start < 0) start = 0;

        float cy = ImGui::GetWindowPos().y + 4.f;
        for (int i = start; i < (int)_history.size(); i++) {
            const ChatEntry& e = _history[i];

            // Fade alpha
            float age   = appTime - e.timestamp;
            float alpha = 1.f;
            if (!_inputOpen) {
                if (age > FADE_AFTER + FADE_DURATION) { cy += lineH; continue; }
                if (age > FADE_AFTER)
                    alpha = 1.f - (age - FADE_AFTER) / FADE_DURATION;
            }

            uint8_t a8 = (uint8_t)(alpha * 255.f);
            float wx = ImGui::GetWindowPos().x + 4.f;

            if (e.username.empty()) {
                // System message — italic yellow
                dl->AddText(nullptr, 0.f, {wx, cy},
                            IM_COL32(220, 200, 80, a8), e.text.c_str());
            } else {
                // Username in cyan, message in white
                std::string prefix = e.username + ": ";
                ImVec2 psz = ImGui::CalcTextSize(prefix.c_str());
                dl->AddText(nullptr, 0.f, {wx, cy},
                            IM_COL32(80, 200, 220, a8), prefix.c_str());
                dl->AddText(nullptr, 0.f, {wx + psz.x, cy},
                            IM_COL32(230, 230, 230, a8), e.text.c_str());
            }
            cy += lineH;
        }

        if (_scrollToBottom) {
            ImGui::SetScrollHereY(1.f);
            _scrollToBottom = false;
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Input bar — only shown when open
    // Returns true while open (suppresses game input)
    bool drawInput(ENetPeer* server) {
        ImGuiIO& io = ImGui::GetIO();
        float winW  = 460.f;
        float x     = 10.f;
        float y     = io.DisplaySize.y - 36.f;

        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({winW, 30.f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.f, 4.f});
        ImGui::Begin("##chatinput", nullptr,
            ImGuiWindowFlags_NoDecoration   |
            ImGuiWindowFlags_NoNav          |
            ImGuiWindowFlags_NoMove         |
            ImGuiWindowFlags_NoSavedSettings|
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (_focusNext) {
            ImGui::SetKeyboardFocusHere();
            _focusNext = false;
        }

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
        ImGui::PushItemWidth(winW - 8.f);

        bool submitted = ImGui::InputText("##chatbox", _inputBuf, sizeof(_inputBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar();

        if (submitted) {
            std::string text(_inputBuf);
            if (!text.empty() && server) {
                ChatMessagePacket pkt{text};
                Net::sendReliable(server, pkt.serialize());
            }
            closeInput();
        }

        return true; // consuming input while open
    }
};
