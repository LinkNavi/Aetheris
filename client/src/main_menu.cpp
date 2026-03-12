#include "main_menu.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <algorithm>

// ── Colour palette ────────────────────────────────────────────────────────────
static constexpr ImU32 COL_BG_DARK    = IM_COL32(6,   5,   8,   255);
static constexpr ImU32 COL_BG_MID     = IM_COL32(14,  11,  18,  255);
static constexpr ImU32 COL_PANEL      = IM_COL32(18,  14,  24,  230);
static constexpr ImU32 COL_PANEL_EDGE = IM_COL32(80,  58,  32,  180);
static constexpr ImU32 COL_GOLD       = IM_COL32(200, 160, 60,  255);
static constexpr ImU32 COL_GOLD_DIM   = IM_COL32(120, 88,  28,  180);
static constexpr ImU32 COL_GOLD_GLOW  = IM_COL32(220, 180, 80,  40);
static constexpr ImU32 COL_WHITE      = IM_COL32(240, 232, 210, 255);
static constexpr ImU32 COL_WHITE_DIM  = IM_COL32(160, 148, 120, 180);
static constexpr ImU32 COL_RED        = IM_COL32(200, 50,  40,  255);
static constexpr ImU32 COL_BTN_HOVER  = IM_COL32(40,  28,  12,  200);
static constexpr ImU32 COL_BTN_NORM   = IM_COL32(22,  16,  10,  160);
static constexpr ImU32 COL_EMBER      = IM_COL32(255, 120, 20,  200);

// Alpha-blend a colour
static ImU32 colAlpha(ImU32 col, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(col);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// Lerp two colours
static ImU32 colLerp(ImU32 a, ImU32 b, float t) {
    ImVec4 va = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 vb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32({
        va.x + (vb.x-va.x)*t, va.y + (vb.y-va.y)*t,
        va.z + (vb.z-va.z)*t, va.w + (vb.w-va.w)*t
    });
}

static float easOutCubic(float t) { float s = t-1; return s*s*s+1.f; }

// ── Settings persistence ──────────────────────────────────────────────────────
void GameSettings::save(const char* path) const {
    std::ofstream f(path);
    if (!f) return;
    f << "render_distance " << renderDistance << "\n";
    f << "vsync "           << (int)vsync     << "\n";
    f << "fov "             << fov            << "\n";
    f << "mouse_sens "      << mouseSens      << "\n";
    f << "master_vol "      << masterVolume   << "\n";
    f << "music_vol "       << musicVolume    << "\n";
    f << "sfx_vol "         << sfxVolume      << "\n";
    f << "last_server "     << lastServer     << "\n";
    f << "server_port "     << serverPort     << "\n";
}

void GameSettings::load(const char* path) {
    std::ifstream f(path);
    if (!f) return;
    std::string key;
    while (f >> key) {
        if      (key == "render_distance") f >> renderDistance;
        else if (key == "vsync")           { int v; f >> v; vsync = v; }
        else if (key == "fov")             f >> fov;
        else if (key == "mouse_sens")      f >> mouseSens;
        else if (key == "master_vol")      f >> masterVolume;
        else if (key == "music_vol")       f >> musicVolume;
        else if (key == "sfx_vol")         f >> sfxVolume;
        else if (key == "last_server")     f >> lastServer;
        else if (key == "server_port")     f >> serverPort;
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────
MainMenu::MainMenu() {
    _settings.load();
    snprintf(_serverInput, sizeof(_serverInput), "%s", _settings.lastServer);
    _portInput = _settings.serverPort;
}

// ── Particle system ───────────────────────────────────────────────────────────
void MainMenu::spawnParticle(int i, int sw, int sh) {
    // Use a simple LCG so it's deterministic per slot but varied
    auto rng = [&](float mn, float mx) -> float {
        static uint32_t s = 0x12345678u;
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return mn + (float)(s & 0xffff) / 65535.f * (mx - mn);
    };
    _particles[i].x          = rng(0.f, 1.f);
    _particles[i].y          = rng(0.7f, 1.1f); // start near bottom
    _particles[i].vx         = rng(-0.004f, 0.004f);
    _particles[i].vy         = rng(-0.030f, -0.012f);
    _particles[i].life        = rng(0.3f, 1.f);
    _particles[i].size        = rng(1.2f, 3.5f);
    _particles[i].brightness  = rng(0.4f, 1.f);
}

void MainMenu::tickParticles(float dt, int sw, int sh) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        auto& p = _particles[i];
        if (p.life <= 0.f) { spawnParticle(i, sw, sh); continue; }
        p.x    += p.vx * dt;
        p.y    += p.vy * dt;
        p.life -= dt * 0.18f;
    }
}

void MainMenu::drawParticles(ImDrawList* dl, int sw, int sh) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        auto& p = _particles[i];
        if (p.life <= 0.f) continue;
        float alpha = p.life * p.brightness;
        float flicker = 0.7f + 0.3f * std::sin(_time * 8.f + i * 1.3f);
        alpha *= flicker;

        // Ember glow (larger soft circle behind)
        float gx = p.x * sw, gy = p.y * sh;
        ImU32 outerCol = colAlpha(COL_EMBER, alpha * 0.15f);
        ImU32 innerCol = colAlpha(COL_GOLD,  alpha * 0.90f);
        dl->AddCircleFilled({gx, gy}, p.size * 3.f, outerCol);
        dl->AddCircleFilled({gx, gy}, p.size,        innerCol);
    }
}

// ── Background ────────────────────────────────────────────────────────────────
static void drawBackground(ImDrawList* dl, float time, int sw, int sh) {
    // Deep gradient
    dl->AddRectFilledMultiColor(
        {0,0}, {(float)sw, (float)sh},
        COL_BG_DARK, COL_BG_DARK,
        COL_BG_MID,  COL_BG_MID
    );

    // Vignette rings
    float cx = sw * 0.5f, cy = sh * 0.55f;
    float pulse = 0.5f + 0.5f * std::sin(time * 0.4f);
    for (int r = 5; r >= 0; r--) {
        float rad   = sw * (0.3f + r * 0.12f);
        float alpha = (0.03f - r * 0.004f) * (r == 0 ? 1.f + pulse * 0.5f : 1.f);
        dl->AddCircleFilled({cx, cy}, rad, colAlpha(COL_GOLD_GLOW, alpha), 64);
    }

    // Subtle horizontal scan lines
    for (int y = 0; y < sh; y += 4) {
        dl->AddLine({0.f,(float)y}, {(float)sw,(float)y},
                    IM_COL32(0,0,0,18), 1.f);
    }

    // Corner ornament lines (top-left)
    float orn = 60.f;
    dl->AddLine({0,0}, {orn, 0},   COL_GOLD_DIM, 1.f);
    dl->AddLine({0,0}, {0,  orn},  COL_GOLD_DIM, 1.f);
    // top-right
    dl->AddLine({(float)sw,0}, {(float)sw-orn, 0},  COL_GOLD_DIM, 1.f);
    dl->AddLine({(float)sw,0}, {(float)sw,     orn}, COL_GOLD_DIM, 1.f);
    // bottom-left
    dl->AddLine({0,(float)sh}, {orn, (float)sh},  COL_GOLD_DIM, 1.f);
    dl->AddLine({0,(float)sh}, {0, (float)sh-orn},COL_GOLD_DIM, 1.f);
    // bottom-right
    dl->AddLine({(float)sw,(float)sh},{(float)sw-orn,(float)sh}, COL_GOLD_DIM, 1.f);
    dl->AddLine({(float)sw,(float)sh},{(float)sw,(float)sh-orn}, COL_GOLD_DIM, 1.f);
}

// ── Title text ────────────────────────────────────────────────────────────────
static void drawTitle(ImDrawList* dl, float cx, float sy, float time) {
    const char* title = "AETHERIS";
    ImFont* font = ImGui::GetFont();
    float   sz   = 72.f;

    // Measure
    ImVec2 tsz = font->CalcTextSizeA(sz, 9999.f, 0.f, title);
    float tx = cx - tsz.x * 0.5f;
    float ty = sy;

    // Glow layers behind text
    float pulse = 0.5f + 0.5f * std::sin(time * 1.1f);
    for (int i = 4; i >= 1; i--) {
        float spread = i * 3.f;
        ImU32 gc = colAlpha(COL_GOLD_GLOW, 0.12f * (5.f - i) * pulse);
        dl->AddText(font, sz, {tx - spread, ty - spread * 0.5f}, gc, title);
        dl->AddText(font, sz, {tx + spread, ty - spread * 0.5f}, gc, title);
    }

    // Gold shadow
    dl->AddText(font, sz, {tx+2, ty+3}, IM_COL32(80,50,0,180), title);
    // Main text
    dl->AddText(font, sz, {tx, ty}, COL_GOLD, title);

    // Subtitle
    const char* sub = "AN OPEN WORLD AWAITS";
    ImVec2 ssz = font->CalcTextSizeA(16.f, 9999.f, 0.f, sub);
    dl->AddText(font, 16.f, {cx - ssz.x*0.5f, ty + tsz.y + 4.f},
                colAlpha(COL_WHITE_DIM, 0.7f + 0.3f*pulse), sub);

    // Thin divider
    float dw = 180.f;
    dl->AddLine({cx - dw, ty + tsz.y + 28.f},
                {cx + dw, ty + tsz.y + 28.f},
                colAlpha(COL_GOLD_DIM, 0.8f), 1.f);
}

// ── Custom button ─────────────────────────────────────────────────────────────
bool MainMenu::menuButton(ImDrawList* dl, const char* label,
                           float cx, float y, float w, float h,
                           bool isHovered, bool& outHovered) {
    float x = cx - w * 0.5f;

    // Invisible ImGui button for input
    ImGui::SetCursorScreenPos({x, y});
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##btn", {w, h});
    outHovered   = ImGui::IsItemHovered();
    ImGui::PopID();

    float hov = isHovered ? 1.f : 0.f;

    // Background
    ImU32 bgCol = colLerp(COL_BTN_NORM, COL_BTN_HOVER, hov);
    dl->AddRectFilled({x, y}, {x+w, y+h}, bgCol, 2.f);

    // Left gold accent bar
    float barW = 3.f + hov * 2.f;
    dl->AddRectFilled({x, y+4}, {x+barW, y+h-4},
                      colLerp(COL_GOLD_DIM, COL_GOLD, hov), 1.f);

    // Border
    dl->AddRect({x, y}, {x+w, y+h},
                colLerp(COL_PANEL_EDGE, COL_GOLD, hov * 0.6f), 2.f, 0, 1.f);

    // Hover shine line across top
    if (hov > 0.01f)
        dl->AddLine({x+barW, y}, {x+w, y},
                    colAlpha(COL_GOLD, hov * 0.25f), 1.f);

    // Label
    ImFont* font = ImGui::GetFont();
    float fsz  = 20.f;
    ImVec2 tsz = font->CalcTextSizeA(fsz, 9999.f, 0.f, label);
    float tx   = cx - tsz.x * 0.5f + 8.f; // slight right offset from bar
    float ty   = y  + (h - tsz.y) * 0.5f;

    // Shadow
    dl->AddText(font, fsz, {tx+1, ty+1}, IM_COL32(0,0,0,180), label);
    // Text
    ImU32 textCol = colLerp(COL_WHITE_DIM, COL_WHITE, hov);
    dl->AddText(font, fsz, {tx, ty}, textCol, label);

    return clicked;
}

// ── Panel helper ──────────────────────────────────────────────────────────────
static void drawPanel(ImDrawList* dl, float x, float y, float w, float h,
                      float slide = 0.f) {
    // slide: 0=fully visible, 1=offscreen right
    float ox = slide * w * 0.25f;
    float alpha = 1.f - slide;

    dl->AddRectFilled({x+ox, y}, {x+w+ox, y+h},
                      colAlpha(COL_PANEL, alpha), 4.f);
    // Gold border
    dl->AddRect({x+ox, y}, {x+w+ox, y+h},
                colAlpha(COL_PANEL_EDGE, alpha * 0.9f), 4.f, 0, 1.2f);
    // Top edge highlight
    dl->AddLine({x+ox+12, y+1}, {x+w+ox-12, y+1},
                colAlpha(COL_GOLD_DIM, alpha * 0.5f), 1.f);
}

// ── Slider widget ─────────────────────────────────────────────────────────────
void MainMenu::drawSlider(ImDrawList* dl, const char* label,
                           float x, float y, float w,
                           float& value, float mn, float mx, const char* fmt) {
    ImFont* font = ImGui::GetFont();
    float trackH = 4.f;
    float knobR  = 7.f;
    float labelW = 150.f;
    float trackX = x + labelW;
    float trackW = w - labelW - 60.f;
    float trackY = y + 12.f;

    // Label
    dl->AddText(font, 15.f, {x, y+4}, COL_WHITE_DIM, label);

    // Track bg
    dl->AddRectFilled({trackX, trackY - trackH*0.5f},
                      {trackX + trackW, trackY + trackH*0.5f},
                      IM_COL32(40,30,15,200), 2.f);

    // Fill
    float t = (value - mn) / (mx - mn);
    t = std::clamp(t, 0.f, 1.f);
    dl->AddRectFilled({trackX, trackY - trackH*0.5f},
                      {trackX + trackW*t, trackY + trackH*0.5f},
                      COL_GOLD_DIM, 2.f);

    // Invisible drag zone
    ImGui::SetCursorScreenPos({trackX - knobR, trackY - knobR});
    ImGui::PushID(label);
    ImGui::InvisibleButton("##sl", {trackW + knobR*2, knobR*2});
    bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemActive()) {
        float mx2 = ImGui::GetMousePos().x;
        t = std::clamp((mx2 - trackX) / trackW, 0.f, 1.f);
        value = mn + t * (mx - mn);
    }
    ImGui::PopID();

    // Knob
    float kx = trackX + trackW * t;
    dl->AddCircleFilled({kx, trackY}, knobR,
                        hov ? COL_GOLD : COL_GOLD_DIM, 12);
    dl->AddCircle({kx, trackY}, knobR, IM_COL32(0,0,0,120), 12, 1.5f);

    // Value text
    char buf[32]; snprintf(buf, sizeof(buf), fmt, value);
    ImVec2 vsz = font->CalcTextSizeA(14.f, 999.f, 0.f, buf);
    dl->AddText(font, 14.f,
                {trackX + trackW + 10.f, y + 4},
                COL_WHITE_DIM, buf);
}

// ── Toggle widget ─────────────────────────────────────────────────────────────
void MainMenu::drawToggle(ImDrawList* dl, const char* label,
                           float x, float y, bool& value) {
    ImFont* font = ImGui::GetFont();
    float tw = 38.f, th = 20.f, tr = th * 0.5f;
    float labelW = 150.f;
    float tx = x + labelW;

    dl->AddText(font, 15.f, {x, y+2}, COL_WHITE_DIM, label);

    // Track
    ImU32 trackCol = value ? colAlpha(COL_GOLD_DIM, 0.9f) : IM_COL32(40,30,15,200);
    dl->AddRectFilled({tx, y}, {tx+tw, y+th}, trackCol, tr);
    dl->AddRect({tx, y}, {tx+tw, y+th}, colAlpha(COL_PANEL_EDGE, 0.8f), tr, 0, 1.f);

    // Knob
    float kx = value ? tx + tw - tr : tx + tr;
    dl->AddCircleFilled({kx, y+tr}, tr - 3.f, value ? COL_GOLD : COL_WHITE_DIM, 12);

    // Click
    ImGui::SetCursorScreenPos({tx, y});
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##tog", {tw, th})) value = !value;
    ImGui::PopID();
}

// ── Int input widget ──────────────────────────────────────────────────────────
void MainMenu::drawInputInt(ImDrawList* dl, const char* label,
                             float x, float y, float w,
                             int& value, int mn, int mx) {
    ImFont* font = ImGui::GetFont();
    float labelW  = 150.f;
    float fieldW  = 60.f;
    float btnSize = 20.f;
    float fx = x + labelW;

    dl->AddText(font, 15.f, {x, y+2}, COL_WHITE_DIM, label);

    // - button
    ImGui::SetCursorScreenPos({fx, y});
    ImGui::PushID(label);
    dl->AddRectFilled({fx, y}, {fx+btnSize, y+btnSize}, IM_COL32(40,28,12,200), 2.f);
    dl->AddText(font, 15.f, {fx+6, y+2}, COL_GOLD, "-");
    if (ImGui::InvisibleButton("##dec", {btnSize, btnSize}))
        value = std::max(mn, value - 1);

    // Value field
    float vx = fx + btnSize + 4;
    dl->AddRectFilled({vx, y}, {vx+fieldW, y+btnSize}, IM_COL32(18,14,24,220), 2.f);
    dl->AddRect({vx, y}, {vx+fieldW, y+btnSize}, COL_PANEL_EDGE, 2.f, 0, 1.f);
    char buf[16]; snprintf(buf, sizeof(buf), "%d", value);
    ImVec2 vsz = font->CalcTextSizeA(15.f, 999.f, 0.f, buf);
    dl->AddText(font, 15.f, {vx + (fieldW-vsz.x)*0.5f, y+2}, COL_WHITE, buf);

    // + button
    float bx = vx + fieldW + 4;
    dl->AddRectFilled({bx, y}, {bx+btnSize, y+btnSize}, IM_COL32(40,28,12,200), 2.f);
    dl->AddText(font, 15.f, {bx+4, y+2}, COL_GOLD, "+");
    ImGui::SetCursorScreenPos({bx, y});
    if (ImGui::InvisibleButton("##inc", {btnSize, btnSize}))
        value = std::min(mx, value + 1);
    ImGui::PopID();
}

// ── Section header ────────────────────────────────────────────────────────────
static void drawSectionHeader(ImDrawList* dl, ImFont* font,
                               const char* text, float x, float y, float w) {
    ImVec2 tsz = font->CalcTextSizeA(13.f, 999.f, 0.f, text);
    dl->AddLine({x, y+6}, {x + (w-tsz.x)*0.5f - 8, y+6},
                COL_GOLD_DIM, 1.f);
    dl->AddText(font, 13.f, {x+(w-tsz.x)*0.5f, y}, COL_GOLD_DIM, text);
    dl->AddLine({x+(w+tsz.x)*0.5f + 8, y+6}, {x+w, y+6},
                COL_GOLD_DIM, 1.f);
}

// ── Tab bar ───────────────────────────────────────────────────────────────────
static bool drawTab(ImDrawList* dl, ImFont* font,
                    const char* label, float x, float y, float w, float h,
                    bool active) {
    ImU32 bg  = active ? IM_COL32(40,28,12,230) : IM_COL32(18,14,10,160);
    ImU32 top = active ? COL_GOLD              : COL_GOLD_DIM;
    dl->AddRectFilled({x,y},{x+w,y+h}, bg, 3.f);
    if (active) dl->AddLine({x+2,y},{x+w-2,y}, top, 2.f);
    else        dl->AddLine({x,y+h},{x+w,y+h}, COL_PANEL_EDGE, 1.f);

    ImVec2 tsz = font->CalcTextSizeA(15.f, 999.f, 0.f, label);
    dl->AddText(font, 15.f,
                {x+(w-tsz.x)*0.5f, y+(h-tsz.y)*0.5f},
                active ? COL_WHITE : COL_WHITE_DIM, label);

    ImGui::SetCursorScreenPos({x, y});
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##tab", {w, h});
    ImGui::PopID();
    return clicked;
}

// ── Main menu page ────────────────────────────────────────────────────────────
GameState MainMenu::drawMainPage(ImDrawList* dl, float cx, float cy,
                                  int sw, int sh, float dt) {
    // Title sits in upper third
    float titleY = sh * 0.12f;
    drawTitle(dl, cx, titleY, _time);

    // Button panel
    float panW  = 280.f;
    float panH  = 340.f;
    float panX  = cx - panW * 0.5f;
    float panY  = sh * 0.38f;
    drawPanel(dl, panX, panY, panW, panH, _panelSlide);

    float btnW  = panW - 40.f;
    float btnH  = 44.f;
    float btnX  = cx;
    float gap   = 10.f;
    float startY = panY + 20.f;

    struct Btn { const char* label; GameState target; };
    static const Btn BTNS[] = {
        {"PLAY SINGLEPLAYER", GameState::WorldSelect },
        {"PLAY MULTIPLAYER",  GameState::Connecting  },
        {"SETTINGS",          GameState::Settings    },
    };

    for (int i = 0; i < 3; i++) {
        bool hov = false;
        float y = startY + i * (btnH + gap);
        bool clicked = menuButton(dl, BTNS[i].label, btnX, y, btnW, btnH,
                                  _hoveredBtn == i, hov);
        if (hov) _hoveredBtn = i;
        if (clicked) {
            _panelSlide = 0.f; // reset for next panel
            return BTNS[i].target;
        }
    }

    // Quit button smaller at bottom
    {
        bool hov = false;
        float y = startY + 3 * (btnH + gap) + 10.f;
        bool clicked = menuButton(dl, "QUIT", btnX, y, btnW * 0.5f, btnH * 0.75f,
                                  _hoveredBtn == 3, hov);
        if (hov) _hoveredBtn = 3;
        if (clicked) {
            // Signal quit (caller can check for a special state)
            // Using Connecting as a dummy — caller checks pendingServerIP == ""
            pendingServerIP = "__QUIT__";
            return GameState::Connecting;
        }
    }

    // Version watermark
    ImFont* font = ImGui::GetFont();
    dl->AddText(font, 12.f, {8.f, (float)sh - 20.f},
                IM_COL32(80, 68, 40, 160), "v0.1.0-alpha");

    return GameState::MainMenu;
}

// ── Settings page ─────────────────────────────────────────────────────────────
GameState MainMenu::drawSettings(ImDrawList* dl, float cx, float cy,
                                  int sw, int sh, float dt) {
    float panW = 540.f, panH = 440.f;
    float panX = cx - panW * 0.5f, panY = cy - panH * 0.5f;
    drawPanel(dl, panX, panY, panW, panH, _panelSlide);

    ImFont* font = ImGui::GetFont();

    // Header
    const char* hdr = "SETTINGS";
    ImVec2 hsz = font->CalcTextSizeA(28.f, 999.f, 0.f, hdr);
    dl->AddText(font, 28.f, {cx-hsz.x*0.5f, panY+14.f}, COL_GOLD, hdr);
    dl->AddLine({panX+20, panY+50.f}, {panX+panW-20, panY+50.f},
                COL_GOLD_DIM, 1.f);

    // Tab bar
    static const char* TABS[] = {"GRAPHICS","AUDIO","NETWORK"};
    float tabW = (panW - 40.f) / 3.f, tabH = 28.f;
    float tabY = panY + 56.f;
    for (int i = 0; i < 3; i++) {
        if (drawTab(dl, font, TABS[i],
                    panX + 20.f + i * tabW, tabY, tabW, tabH,
                    _settingsTab == i))
            _settingsTab = i;
    }

    // Content area
    float cy2   = panY + 56.f + tabH + 20.f;
    float lx    = panX + 30.f;
    float rowH  = 34.f;

    if (_settingsTab == 0) { // Graphics
        drawSectionHeader(dl, font, "RENDERING", lx, cy2, panW-60.f);
        cy2 += 22.f;
        drawSlider(dl, "Render Distance", lx, cy2, panW-60.f,
                   _settings.renderDistance, 1.f, 8.f, "%.0f chunks");
        cy2 += rowH;
        drawSlider(dl, "Field of View", lx, cy2, panW-60.f,
                   *(float*)&_settings.fov, // reinterpret for slider
                   60.f, 110.f, "%.0f°");
        cy2 += rowH;
        drawToggle(dl, "VSync", lx, cy2, _settings.vsync);
        cy2 += rowH + 10.f;
        drawSectionHeader(dl, font, "INPUT", lx, cy2, panW-60.f);
        cy2 += 22.f;
        drawSlider(dl, "Mouse Sensitivity", lx, cy2, panW-60.f,
                   _settings.mouseSens, 0.01f, 0.5f, "%.3f");
    } else if (_settingsTab == 1) { // Audio
        drawSectionHeader(dl, font, "VOLUME", lx, cy2, panW-60.f);
        cy2 += 22.f;
        drawSlider(dl, "Master Volume", lx, cy2, panW-60.f,
                   _settings.masterVolume, 0.f, 1.f, "%.0f%%");
        _settings.masterVolume = std::clamp(_settings.masterVolume, 0.f, 1.f);
        cy2 += rowH;
        drawSlider(dl, "Music Volume", lx, cy2, panW-60.f,
                   _settings.musicVolume, 0.f, 1.f, "%.0f%%");
        cy2 += rowH;
        drawSlider(dl, "SFX Volume", lx, cy2, panW-60.f,
                   _settings.sfxVolume, 0.f, 1.f, "%.0f%%");
    } else { // Network
        drawSectionHeader(dl, font, "SERVER", lx, cy2, panW-60.f);
        cy2 += 22.f;
        // IP input using ImGui InputText styled inline
        dl->AddText(font, 15.f, {lx, cy2+2}, COL_WHITE_DIM, "Default Server IP");
        float fx = lx + 150.f;
        dl->AddRectFilled({fx, cy2}, {fx+200, cy2+22}, IM_COL32(18,14,24,220), 2.f);
        dl->AddRect({fx, cy2}, {fx+200, cy2+22}, COL_PANEL_EDGE, 2.f, 0, 1.f);
        ImGui::SetCursorScreenPos({fx+4, cy2+2});
        ImGui::PushStyleColor(ImGuiCol_FrameBg,         ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text,            ImVec4(0.94f,0.91f,0.82f,1));
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg,  ImVec4(0.55f,0.40f,0.10f,0.8f));
        ImGui::PushItemWidth(192.f);
        ImGui::InputText("##srvip", _settings.lastServer,
                         sizeof(_settings.lastServer),
                         ImGuiInputTextFlags_None);
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);

        cy2 += rowH;
        drawInputInt(dl, "Default Port", lx, cy2, panW-60.f,
                     _settings.serverPort, 1024, 65535);
    }

    // Back + Apply buttons at bottom
    float bbY = panY + panH - 54.f;
    bool hBack = false, hApply = false;
    if (menuButton(dl, "BACK", panX + 80.f, bbY, 100.f, 36.f,
                   _hoveredBtn == 10, hBack)) {
        _settings.save();
        _panelSlide = 1.f;
        return GameState::MainMenu;
    }
    if (hBack) _hoveredBtn = 10;

    if (menuButton(dl, "APPLY & SAVE", panX + panW - 180.f, bbY, 160.f, 36.f,
                   _hoveredBtn == 11, hApply)) {
        _settings.save();
    }
    if (hApply) _hoveredBtn = 11;

    return GameState::Settings;
}

// ── World select page ─────────────────────────────────────────────────────────
GameState MainMenu::drawWorldSel(ImDrawList* dl, float cx, float cy,
                                  int sw, int sh, float dt) {
    float panW = 500.f, panH = 380.f;
    float panX = cx - panW*0.5f, panY = cy - panH*0.5f;
    drawPanel(dl, panX, panY, panW, panH, _panelSlide);

    ImFont* font = ImGui::GetFont();
    const char* hdr = "SELECT WORLD";
    ImVec2 hsz = font->CalcTextSizeA(26.f, 999.f, 0.f, hdr);
    dl->AddText(font, 26.f, {cx-hsz.x*0.5f, panY+14.f}, COL_GOLD, hdr);
    dl->AddLine({panX+20, panY+48.f},{panX+panW-20, panY+48.f},COL_GOLD_DIM,1.f);

    // Placeholder worlds
    static const char* WORLDS[] = {"World 1","World 2","World 3"};
    static const char* SUBTXT[] = {"Last played: Today","Last played: Yesterday","New World"};
    float wy = panY + 62.f;
    for (int i = 0; i < 3; i++) {
        float rx = panX + 20.f, rw = panW - 40.f, rh = 54.f;
        bool hover = ImGui::IsMouseHoveringRect({rx,wy},{rx+rw,wy+rh});
        ImU32 rbg = hover ? IM_COL32(40,28,12,200) : IM_COL32(22,16,10,140);
        dl->AddRectFilled({rx,wy},{rx+rw,wy+rh}, rbg, 3.f);
        dl->AddRect({rx,wy},{rx+rw,wy+rh},
                    hover ? COL_GOLD_DIM : COL_PANEL_EDGE, 3.f, 0, 1.f);
        // World name
        dl->AddText(font, 18.f, {rx+14, wy+8}, COL_WHITE, WORLDS[i]);
        dl->AddText(font, 13.f, {rx+14, wy+30}, COL_WHITE_DIM, SUBTXT[i]);

        // Click detection
        ImGui::SetCursorScreenPos({rx, wy});
        ImGui::PushID(i + 50);
        if (ImGui::InvisibleButton("##world", {rw, rh})) {
            pendingServerIP   = "127.0.0.1";
            pendingServerPort = _settings.serverPort;
            return GameState::Connecting;
        }
        ImGui::PopID();
        wy += rh + 8.f;
    }

    // Back
    float bbY = panY + panH - 52.f;
    bool hBack = false;
    if (menuButton(dl, "BACK", cx, bbY, 140.f, 36.f, _hoveredBtn==20, hBack)) {
        _panelSlide = 1.f;
        return GameState::MainMenu;
    }
    if (hBack) _hoveredBtn = 20;

    return GameState::WorldSelect;
}

// ── Main draw ─────────────────────────────────────────────────────────────────
GameState MainMenu::draw(float dt, int screenW, int screenH) {
    _time += dt;

    // Slide animation
    float slideTarget = 0.f;
    _panelSlide += (slideTarget - _panelSlide) * std::min(8.f * dt, 1.f);

    tickParticles(dt, screenW, screenH);

    // Push a fullscreen invisible ImGui window
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)screenW, (float)screenH});
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  {0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##menu_root", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    ImGui::End();
    ImGui::PopStyleVar(2);

    // Draw on background draw list so it's always behind everything
    ImDrawList* dl   = ImGui::GetBackgroundDrawList();
    float cx = screenW * 0.5f;
    float cy = screenH * 0.5f;

    drawBackground(dl, _time, screenW, screenH);
    drawParticles(dl, screenW, screenH);

    // Route to sub-pages
    GameState next = _state;
    switch (_state) {
    case GameState::MainMenu:
        next = drawMainPage(dl, cx, cy, screenW, screenH, dt);
        break;
    case GameState::Settings:
        next = drawSettings(dl, cx, cy, screenW, screenH, dt);
        break;
    case GameState::WorldSelect:
        next = drawWorldSel(dl, cx, cy, screenW, screenH, dt);
        break;
    default:
        break;
    }

    // On state change, reset slide + hoveredBtn
    if (next != _state) {
        _state      = next;
        _hoveredBtn = -1;
        _panelSlide = 1.f; // will ease to 0
    }

    return _state;
}