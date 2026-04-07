#pragma once
#include <imgui.h>
#include <enet/enet.h>
#include <string>
#include <cmath>
#include "spell_charge_packets.h"
#include "net_common.h"

// ── SpellChargeUI ─────────────────────────────────────────────────────────────
// Draws the cast bar, mana charge meter, and interrupt flash.
// Call draw() every frame inside ImGui context.
// Call onKeyDown / onKeyUp from input handling.
class SpellChargeUI {
public:
    // ── State mirrored from server ─────────────────────────────────────────
    uint8_t phase          = 0;
    float   manaCommitted  = 0.f;
    float   castTimeTotal  = 0.f;
    float   castTimeElapsed= 0.f;
    float   interruptDC    = 0.f;

    // ── Local charge tracking (for responsive feel) ────────────────────────
    float   localMana      = 0.f; // current player mana (from stats sync)
    float   maxMana        = 100.f;

    // ── Current spell being charged ────────────────────────────────────────
    std::string activeSpell;

    // ── Interrupt flash ────────────────────────────────────────────────────
    float interruptFlash = 0.f;

    void applyState(const SpellCastStatePacket& pkt) {
    bool wasWinding = (phase == 2);
    phase = pkt.phase;
    manaCommitted   = pkt.manaCommitted;
    castTimeTotal   = pkt.castTimeTotal;
    castTimeElapsed = pkt.castTimeElapsed;
    interruptDC     = pkt.interruptDC;

    // Only flash if interrupted (phase 0), not if fired (phase 3)
    if (wasWinding && phase == 0) interruptFlash = 0.4f;
    // Reset to idle after successful fire
    if (phase == 3) phase = 0;
}

    void update(float dt) {
        if (interruptFlash > 0.f) interruptFlash -= dt;
        // Locally advance cast time for smooth bar (server corrects each tick)
        if (phase == 2 && castTimeTotal > 0.f)
            castTimeElapsed = std::min(castTimeElapsed + dt, castTimeTotal);
    }

    // ── Input ──────────────────────────────────────────────────────────────
    // Returns true if the key was consumed
    bool onKeyDown(int glfwKey, const std::string& spellName,
                   float aimX, float aimY, float aimZ,
                   ENetPeer* server) {
        if (phase != 0) return false; // already casting
        if (spellName.empty()) return false;

        activeSpell = spellName;
        SpellChargeBeginPacket pkt;
        pkt.spellName = spellName;
        pkt.aimX = aimX; pkt.aimY = aimY; pkt.aimZ = aimZ;
        Net::sendReliable(server, pkt.serialize());
        phase = 1; // optimistic local state
        manaCommitted = 0.f;
        return true;
    }

    void onKeyUp(ENetPeer* server) {
        if (phase == 1) {
            // Release → commit
            Net::sendReliable(server, SpellChargeCommitPacket{}.serialize());
        }
    }

    void onCancel(ENetPeer* server) {
        if (phase == 0) return;
        Net::sendReliable(server, SpellChargeCancelPacket{}.serialize());
        phase = 0;
        manaCommitted = 0.f;
        activeSpell.clear();
    }

    // ── Draw ───────────────────────────────────────────────────────────────
    void draw() const {
        if (phase == 0 && interruptFlash <= 0.f) return;

        ImGuiIO& io = ImGui::GetIO();
        float sw = io.DisplaySize.x;
        float sh = io.DisplaySize.y;

        // Centered bar, above hotbar
        float barW = 360.f;
        float barH = 18.f;
        float x    = (sw - barW) * 0.5f;
        float y    = sh - 160.f;

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // Interrupt flash — red screen edge
        if (interruptFlash > 0.f) {
            float a = interruptFlash / 0.4f;
            dl->AddRectFilled({0,0},{sw,sh}, IM_COL32(220,30,30,(uint8_t)(80*a)));
            const char* txt = "INTERRUPTED";
            ImVec2 tsz = ImGui::CalcTextSize(txt);
            dl->AddText({sw*0.5f - tsz.x*0.5f, sh*0.4f},
                        IM_COL32(255,80,80,(uint8_t)(255*a)), txt);
            return;
        }

        // Spell name label
        if (!activeSpell.empty()) {
            ImVec2 tsz = ImGui::CalcTextSize(activeSpell.c_str());
            dl->AddText({x + (barW-tsz.x)*0.5f, y - 22.f},
                        IM_COL32(200,180,255,220), activeSpell.c_str());
        }

        // Background track
        dl->AddRectFilled({x, y}, {x+barW, y+barH}, IM_COL32(15,10,25,200), 4.f);
        dl->AddRect      ({x, y}, {x+barW, y+barH}, IM_COL32(100,60,180,180), 4.f, 0, 1.5f);

        if (phase == 1) {
            // Charging — show mana bar (cyan/blue)
            float frac = (maxMana > 0.f) ? manaCommitted / maxMana : 0.f;
            frac = std::min(frac, 1.f);
            if (frac > 0.001f) {
                dl->AddRectFilled({x, y}, {x+barW*frac, y+barH},
                                  IM_COL32(60,160,255,220), 4.f);
            }
            // Pulse effect
            float pulse = 0.7f + 0.3f * std::sin((float)ImGui::GetTime() * 8.f);
            dl->AddRect({x,y},{x+barW,y+barH},
                        IM_COL32(60,160,255,(uint8_t)(120*pulse)),4.f,0,2.f);

            // Mana label
            char buf[64];
            snprintf(buf, sizeof(buf), "%.0f mana", manaCommitted);
            ImVec2 tsz = ImGui::CalcTextSize(buf);
            dl->AddText({x+(barW-tsz.x)*0.5f, y+(barH-tsz.y)*0.5f},
                        IM_COL32(200,230,255,255), buf);

        } else if (phase == 2) {
            // Wind-up — show cast time progress (purple/gold)
            float frac = (castTimeTotal > 0.f) ?
                         castTimeElapsed / castTimeTotal : 0.f;
            frac = std::min(frac, 1.f);

            // Background fill (dark purple)
            dl->AddRectFilled({x,y},{x+barW,y+barH}, IM_COL32(40,20,60,180), 4.f);
            // Progress fill (gold → white near completion)
            if (frac > 0.001f) {
                ImU32 col = frac > 0.85f
                    ? IM_COL32(255,255,180,230)
                    : IM_COL32(200,130,255,220);
                dl->AddRectFilled({x,y},{x+barW*frac,y+barH}, col, 4.f);
            }

            // Cast time remaining label
            char buf[64];
            float remaining = castTimeTotal - castTimeElapsed;
            snprintf(buf, sizeof(buf), "%.1fs  (%.0f mana)", remaining, manaCommitted);
            ImVec2 tsz = ImGui::CalcTextSize(buf);
            dl->AddText({x+(barW-tsz.x)*0.5f, y+(barH-tsz.y)*0.5f},
                        IM_COL32(240,220,255,255), buf);

            // Interrupt DC hint — small text below bar
            char dc[64];
            snprintf(dc, sizeof(dc), "interrupt DC: %.0f dmg", interruptDC);
            ImVec2 dcsz = ImGui::CalcTextSize(dc);
            dl->AddText({x+(barW-dcsz.x)*0.5f, y+barH+4.f},
                        IM_COL32(160,120,200,160), dc);
        }
    }
};
