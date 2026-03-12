#include "main_menu.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <algorithm>

// ── Colour palette — dark teal/blue with warm amber accents ───────────────────
static constexpr ImU32 COL_BG_DARK    = IM_COL32(6,   10,  14,  255);
static constexpr ImU32 COL_BG_MID     = IM_COL32(10,  16,  22,  255);
static constexpr ImU32 COL_PANEL      = IM_COL32(12,  18,  26,  235);
static constexpr ImU32 COL_PANEL_EDGE = IM_COL32(40,  65,  80,  180);
static constexpr ImU32 COL_GREY       = IM_COL32(140, 155, 165, 255);
static constexpr ImU32 COL_GREY_DIM   = IM_COL32(55,  70,  80,  180);
static constexpr ImU32 COL_GREY_GLOW  = IM_COL32(100, 160, 200, 25);
static constexpr ImU32 COL_WHITE      = IM_COL32(210, 218, 225, 255);
static constexpr ImU32 COL_WHITE_DIM  = IM_COL32(110, 125, 140, 180);
static constexpr ImU32 COL_RED        = IM_COL32(200, 60,  50,  255);
static constexpr ImU32 COL_BTN_HOVER  = IM_COL32(25,  40,  55,  220);
static constexpr ImU32 COL_BTN_NORM   = IM_COL32(14,  22,  32,  180);
static constexpr ImU32 COL_GREEN      = IM_COL32(50,  190, 80,  255);
static constexpr ImU32 COL_AMBER      = IM_COL32(200, 155, 50,  255);
static constexpr ImU32 COL_AMBER_DIM  = IM_COL32(140, 100, 30,  120);
static constexpr ImU32 COL_TEAL       = IM_COL32(40,  140, 160, 255);
static constexpr ImU32 COL_TEAL_DIM   = IM_COL32(20,  80,  100, 120);

static ImU32 colAlpha(ImU32 col, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(col);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}
static ImU32 colLerp(ImU32 a, ImU32 b, float t) {
    ImVec4 va = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 vb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32({
        va.x+(vb.x-va.x)*t, va.y+(vb.y-va.y)*t,
        va.z+(vb.z-va.z)*t, va.w+(vb.w-va.w)*t});
}

// ── Settings persistence ──────────────────────────────────────────────────────
void GameSettings::save(const char* path) const {
    std::ofstream f(path);
    if (!f) return;
    f << "render_distance " << renderDistance << "\n"
      << "vsync "           << (int)vsync     << "\n"
      << "fov "             << fov            << "\n"
      << "mouse_sens "      << mouseSens      << "\n"
      << "master_vol "      << masterVolume   << "\n"
      << "music_vol "       << musicVolume    << "\n"
      << "sfx_vol "         << sfxVolume      << "\n"
      << "last_server "     << lastServer     << "\n"
      << "server_port "     << serverPort     << "\n"
      << "auth_port "       << authPort       << "\n";
}
void GameSettings::load(const char* path) {
    std::ifstream f(path);
    if (!f) return;
    std::string key;
    while (f >> key) {
        if      (key=="render_distance") f>>renderDistance;
        else if (key=="vsync")           { int v; f>>v; vsync=v; }
        else if (key=="fov")             f>>fov;
        else if (key=="mouse_sens")      f>>mouseSens;
        else if (key=="master_vol")      f>>masterVolume;
        else if (key=="music_vol")       f>>musicVolume;
        else if (key=="sfx_vol")         f>>sfxVolume;
        else if (key=="last_server")     f>>lastServer;
        else if (key=="server_port")     f>>serverPort;
        else if (key=="auth_port")       f>>authPort;
    }
    fovF = (float)fov;
}

// ── Constructor ───────────────────────────────────────────────────────────────
MainMenu::MainMenu() {
    _settings.load();
    snprintf(_serverInput, sizeof(_serverInput), "%s", _settings.lastServer);
    _portInput = _settings.serverPort;
}

// ── Particles ─────────────────────────────────────────────────────────────────
void MainMenu::spawnParticle(int i, int sw, int sh) {
    static uint32_t s = 0x12345678u;
    auto rng = [&](float mn, float mx) -> float {
        s ^= s<<13; s ^= s>>17; s ^= s<<5;
        return mn + (float)(s & 0xffff)/65535.f*(mx-mn);
    };
    _particles[i] = { rng(0,1), rng(0.7f,1.1f), rng(-0.004f,0.004f),
                      rng(-0.030f,-0.012f), rng(0.3f,1.f),
                      rng(1.2f,3.5f), rng(0.4f,1.f) };
}
void MainMenu::tickParticles(float dt, int sw, int sh) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        auto& p = _particles[i];
        if (p.life <= 0.f) { spawnParticle(i, sw, sh); continue; }
        p.x += p.vx*dt; p.y += p.vy*dt; p.life -= dt*0.18f;
    }
}
void MainMenu::drawParticles(ImDrawList* dl, int sw, int sh) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        auto& p = _particles[i];
        if (p.life <= 0.f) continue;
        float alpha = p.life * p.brightness * (0.7f + 0.3f*std::sin(_time*8.f+i*1.3f));
        float gx = p.x*sw, gy = p.y*sh;
        dl->AddCircleFilled({gx,gy}, p.size*3.f, colAlpha(COL_GREY_GLOW, alpha*0.15f));
        dl->AddCircleFilled({gx,gy}, p.size,     colAlpha(COL_TEAL,      alpha*0.45f));
    }
}

// ── Background ────────────────────────────────────────────────────────────────
static void drawBackground(ImDrawList* dl, float time, int sw, int sh) {
    dl->AddRectFilledMultiColor({0,0},{(float)sw,(float)sh},
        COL_BG_DARK,COL_BG_DARK,COL_BG_MID,COL_BG_MID);
    float cx=sw*0.5f, cy=sh*0.55f;
    float pulse=0.5f+0.5f*std::sin(time*0.4f);
    for (int r=5;r>=0;r--) {
        float rad=sw*(0.3f+r*0.12f);
        float alpha=(0.02f-r*0.0025f)*(r==0?1.f+pulse*0.3f:1.f);
        dl->AddCircleFilled({cx,cy},rad,colAlpha(COL_TEAL_DIM,alpha),64);
    }
    // Subtle scanlines
    for (int y=0;y<sh;y+=4)
        dl->AddLine({0.f,(float)y},{(float)sw,(float)y},IM_COL32(0,0,0,10),1.f);
    // Corner ornaments
    float orn=60.f;
    ImU32 ornCol = COL_GREY_DIM;
    dl->AddLine({0,0},{orn,0},ornCol,1.f);  dl->AddLine({0,0},{0,orn},ornCol,1.f);
    dl->AddLine({(float)sw,0},{(float)sw-orn,0},ornCol,1.f);
    dl->AddLine({(float)sw,0},{(float)sw,orn},ornCol,1.f);
    dl->AddLine({0,(float)sh},{orn,(float)sh},ornCol,1.f);
    dl->AddLine({0,(float)sh},{0,(float)sh-orn},ornCol,1.f);
    dl->AddLine({(float)sw,(float)sh},{(float)sw-orn,(float)sh},ornCol,1.f);
    dl->AddLine({(float)sw,(float)sh},{(float)sw,(float)sh-orn},ornCol,1.f);
}

static void drawTitle(ImDrawList* dl, float cx, float sy, float time) {
    const char* title = "AETHERIS";
    ImFont* font = ImGui::GetFont();
    float sz = 72.f;
    ImVec2 tsz = font->CalcTextSizeA(sz,9999.f,0.f,title);
    float tx=cx-tsz.x*0.5f, ty=sy;
    float pulse=0.5f+0.5f*std::sin(time*1.1f);
    for (int i=4;i>=1;i--) {
        float spread=i*2.f;
        ImU32 gc=colAlpha(COL_TEAL_DIM,0.06f*(5.f-i)*pulse);
        dl->AddText(font,sz,{tx-spread,ty-spread*0.5f},gc,title);
        dl->AddText(font,sz,{tx+spread,ty-spread*0.5f},gc,title);
    }
    dl->AddText(font,sz,{tx+2,ty+3},IM_COL32(0,0,0,180),title);
    dl->AddText(font,sz,{tx,ty},COL_WHITE,title);
    const char* sub="AN OPEN WORLD AWAITS";
    ImVec2 ssz=font->CalcTextSizeA(16.f,9999.f,0.f,sub);
    dl->AddText(font,16.f,{cx-ssz.x*0.5f,ty+tsz.y+4.f},colAlpha(COL_WHITE_DIM,0.7f+0.3f*pulse),sub);
    float dw=180.f;
    dl->AddLine({cx-dw,ty+tsz.y+28.f},{cx+dw,ty+tsz.y+28.f},colAlpha(COL_GREY_DIM,0.8f),1.f);
}

// ── Button ────────────────────────────────────────────────────────────────────
bool MainMenu::menuButton(ImDrawList* dl, const char* label,
                           float cx, float y, float w, float h,
                           bool /*unused*/, bool& outHovered) {
    float x = cx - w*0.5f;
    ImGui::SetCursorScreenPos({x, y});
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##btn", {w, h});
    outHovered   = ImGui::IsItemHovered();
    ImGui::PopID();

    float hov = outHovered ? 1.f : 0.f;
    dl->AddRectFilled({x,y},{x+w,y+h}, colLerp(COL_BTN_NORM,COL_BTN_HOVER,hov), 3.f);
    float barW = 3.f + hov*2.f;
    dl->AddRectFilled({x,y+4},{x+barW,y+h-4}, colLerp(COL_TEAL_DIM,COL_TEAL,hov), 1.f);
    dl->AddRect({x,y},{x+w,y+h}, colLerp(COL_PANEL_EDGE,COL_TEAL,hov*0.5f), 3.f, 0, 1.f);

    ImFont* font = ImGui::GetFont();
    float fsz = 20.f;
    ImVec2 tsz = font->CalcTextSizeA(fsz,9999.f,0.f,label);
    float tx2 = cx - tsz.x*0.5f + 8.f, ty2 = y + (h-tsz.y)*0.5f;
    dl->AddText(font,fsz,{tx2+1,ty2+1},IM_COL32(0,0,0,180),label);
    dl->AddText(font,fsz,{tx2,ty2}, colLerp(COL_WHITE_DIM,COL_WHITE,hov), label);
    return clicked;
}

static void drawPanel(ImDrawList* dl, float x, float y, float w, float h, float slide=0.f) {
    float ox=slide*w*0.25f, alpha=1.f-slide;
    dl->AddRectFilled({x+ox,y},{x+w+ox,y+h}, colAlpha(COL_PANEL,alpha), 5.f);
    dl->AddRect({x+ox,y},{x+w+ox,y+h}, colAlpha(COL_PANEL_EDGE,alpha*0.9f), 5.f, 0, 1.2f);
    dl->AddLine({x+ox+12,y+1},{x+w+ox-12,y+1}, colAlpha(COL_TEAL_DIM,alpha*0.4f), 1.f);
}

void MainMenu::drawSlider(ImDrawList* dl, const char* label,
                           float x, float y, float w,
                           float& value, float mn, float mx, const char* fmt) {
    ImFont* font=ImGui::GetFont();
    float trackH=4.f,knobR=7.f,labelW=150.f;
    float trackX=x+labelW, trackW=w-labelW-60.f, trackY=y+12.f;
    dl->AddText(font,15.f,{x,y+4},COL_WHITE_DIM,label);
    dl->AddRectFilled({trackX,trackY-trackH*0.5f},{trackX+trackW,trackY+trackH*0.5f},IM_COL32(20,30,40,200),2.f);
    float t=std::clamp((value-mn)/(mx-mn),0.f,1.f);
    dl->AddRectFilled({trackX,trackY-trackH*0.5f},{trackX+trackW*t,trackY+trackH*0.5f},COL_TEAL_DIM,2.f);
    ImGui::SetCursorScreenPos({trackX-knobR,trackY-knobR});
    ImGui::PushID(label);
    ImGui::InvisibleButton("##sl",{trackW+knobR*2,knobR*2});
    bool hov=ImGui::IsItemHovered();
    if (ImGui::IsItemActive()) {
        t=std::clamp((ImGui::GetMousePos().x-trackX)/trackW,0.f,1.f);
        value=mn+t*(mx-mn);
    }
    ImGui::PopID();
    float kx=trackX+trackW*t;
    dl->AddCircleFilled({kx,trackY},knobR,hov?COL_TEAL:COL_GREY_DIM,12);
    dl->AddCircle({kx,trackY},knobR,IM_COL32(0,0,0,120),12,1.5f);
    char buf[32]; snprintf(buf,sizeof(buf),fmt,value);
    dl->AddText(font,14.f,{trackX+trackW+10.f,y+4},COL_WHITE_DIM,buf);
}

void MainMenu::drawToggle(ImDrawList* dl, const char* label, float x, float y, bool& value) {
    ImFont* font=ImGui::GetFont();
    float tw=38.f,th=20.f,tr=th*0.5f,labelW=150.f,tx=x+labelW;
    dl->AddText(font,15.f,{x,y+2},COL_WHITE_DIM,label);
    ImU32 trackCol=value?colAlpha(COL_TEAL_DIM,0.9f):IM_COL32(20,30,40,200);
    dl->AddRectFilled({tx,y},{tx+tw,y+th},trackCol,tr);
    dl->AddRect({tx,y},{tx+tw,y+th},colAlpha(COL_PANEL_EDGE,0.8f),tr,0,1.f);
    float kx=value?tx+tw-tr:tx+tr;
    dl->AddCircleFilled({kx,y+tr},tr-3.f,value?COL_WHITE:COL_WHITE_DIM,12);
    ImGui::SetCursorScreenPos({tx,y});
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##tog",{tw,th})) value=!value;
    ImGui::PopID();
}

void MainMenu::drawInputInt(ImDrawList* dl, const char* label,
                             float x, float y, float w, int& value, int mn, int mx) {
    ImFont* font=ImGui::GetFont();
    float labelW=150.f,fieldW=60.f,btnSize=20.f,fx=x+labelW;
    dl->AddText(font,15.f,{x,y+2},COL_WHITE_DIM,label);
    ImGui::PushID(label);
    dl->AddRectFilled({fx,y},{fx+btnSize,y+btnSize},IM_COL32(20,30,40,200),2.f);
    dl->AddText(font,15.f,{fx+6,y+2},COL_GREY,"-");
    ImGui::SetCursorScreenPos({fx,y});
    if (ImGui::InvisibleButton("##dec",{btnSize,btnSize})) value=std::max(mn,value-1);
    float vx=fx+btnSize+4;
    dl->AddRectFilled({vx,y},{vx+fieldW,y+btnSize},IM_COL32(10,16,24,220),2.f);
    dl->AddRect({vx,y},{vx+fieldW,y+btnSize},COL_PANEL_EDGE,2.f,0,1.f);
    char buf[16]; snprintf(buf,sizeof(buf),"%d",value);
    ImVec2 vsz=font->CalcTextSizeA(15.f,999.f,0.f,buf);
    dl->AddText(font,15.f,{vx+(fieldW-vsz.x)*0.5f,y+2},COL_WHITE,buf);
    float bx=vx+fieldW+4;
    dl->AddRectFilled({bx,y},{bx+btnSize,y+btnSize},IM_COL32(20,30,40,200),2.f);
    dl->AddText(font,15.f,{bx+4,y+2},COL_GREY,"+");
    ImGui::SetCursorScreenPos({bx,y});
    if (ImGui::InvisibleButton("##inc",{btnSize,btnSize})) value=std::min(mx,value+1);
    ImGui::PopID();
}

void MainMenu::drawTextInput(ImDrawList* dl, const char* label,
                              float x, float y, float fieldW,
                              char* buf, int bufSz, bool password) {
    ImFont* font=ImGui::GetFont();
    float labelW=150.f, fx=x+labelW;
    dl->AddText(font,15.f,{x,y+2},COL_WHITE_DIM,label);
    dl->AddRectFilled({fx,y},{fx+fieldW,y+22},IM_COL32(10,16,24,220),2.f);
    dl->AddRect({fx,y},{fx+fieldW,y+22},COL_PANEL_EDGE,2.f,0,1.f);
    ImGui::SetCursorScreenPos({fx+4,y+2});
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.82f,0.85f,0.88f,1));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.15f,0.35f,0.45f,0.8f));
    ImGui::PushItemWidth(fieldW-8.f);
    ImGui::PushID(label);
    ImGuiInputTextFlags flags = password ? ImGuiInputTextFlags_Password : 0;
    ImGui::InputText("##ti", buf, bufSz, flags);
    ImGui::PopID();
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(3);
}

static void drawSectionHeader(ImDrawList* dl, ImFont* font, const char* text,
                               float x, float y, float w) {
    ImVec2 tsz=font->CalcTextSizeA(13.f,999.f,0.f,text);
    dl->AddLine({x,y+6},{x+(w-tsz.x)*0.5f-8,y+6},COL_GREY_DIM,1.f);
    dl->AddText(font,13.f,{x+(w-tsz.x)*0.5f,y},COL_GREY_DIM,text);
    dl->AddLine({x+(w+tsz.x)*0.5f+8,y+6},{x+w,y+6},COL_GREY_DIM,1.f);
}

static bool drawTab(ImDrawList* dl, ImFont* font, const char* label,
                    float x, float y, float w, float h, bool active) {
    ImU32 bg=active?IM_COL32(20,35,50,230):IM_COL32(12,18,26,160);
    dl->AddRectFilled({x,y},{x+w,y+h},bg,3.f);
    if (active) dl->AddLine({x+2,y},{x+w-2,y},COL_TEAL,2.f);
    else        dl->AddLine({x,y+h},{x+w,y+h},COL_PANEL_EDGE,1.f);
    ImVec2 tsz=font->CalcTextSizeA(15.f,999.f,0.f,label);
    dl->AddText(font,15.f,{x+(w-tsz.x)*0.5f,y+(h-tsz.y)*0.5f},
                active?COL_WHITE:COL_WHITE_DIM,label);
    ImGui::SetCursorScreenPos({x,y});
    ImGui::PushID(label);
    bool clicked=ImGui::InvisibleButton("##tab",{w,h});
    ImGui::PopID();
    return clicked;
}

static ImDrawList* beginFullscreenWindow(int sw, int sh) {
    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize({(float)sw,(float)sh});
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.f);
    ImGui::Begin("##menu_root", nullptr,
        ImGuiWindowFlags_NoDecoration   |
        ImGuiWindowFlags_NoNav          |
        ImGuiWindowFlags_NoMove         |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    return ImGui::GetWindowDrawList();
}
static void endFullscreenWindow() { ImGui::End(); }

// ── Account Button (top-right on main page) ──────────────────────────────────
void MainMenu::drawAccountButton(ImDrawList* dl, int sw, int sh, GameState& next) {
    ImFont* font = ImGui::GetFont();
    float btnW = 130.f, btnH = 32.f;
    float bx = (float)sw - btnW - 16.f, by = 16.f;

    ImGui::SetCursorScreenPos({bx, by});
    ImGui::PushID("acc_btn");
    bool clicked = ImGui::InvisibleButton("##accbtn", {btnW, btnH});
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    ImU32 bg = hov ? COL_BTN_HOVER : COL_BTN_NORM;
    dl->AddRectFilled({bx,by},{bx+btnW,by+btnH}, bg, 4.f);
    dl->AddRect({bx,by},{bx+btnW,by+btnH}, hov ? COL_TEAL : COL_PANEL_EDGE, 4.f, 0, 1.f);

    const char* label = _account.loggedIn ? _account.username : "Account";
    ImU32 dotCol = _account.loggedIn ? COL_GREEN : COL_GREY_DIM;

    // Status dot
    dl->AddCircleFilled({bx+14, by+btnH*0.5f}, 4.f, dotCol, 8);

    ImVec2 tsz = font->CalcTextSizeA(14.f, 999.f, 0.f, label);
    float maxTextW = btnW - 34.f;
    // Truncate if needed
    dl->AddText(font, 14.f, {bx+24, by+(btnH-tsz.y)*0.5f},
                hov ? COL_WHITE : COL_WHITE_DIM, label);

    if (clicked) {
        _panelSlide = 1.f;
        next = GameState::Account;
    }
}

// ── Pages ─────────────────────────────────────────────────────────────────────
GameState MainMenu::drawMainPage(ImDrawList* dl, float cx, float cy,
                                  int sw, int sh, float dt) {
    drawTitle(dl, cx, sh*0.12f, _time);

    GameState next = GameState::MainMenu;

    // Account button top-right
    drawAccountButton(dl, sw, sh, next);
    if (next != GameState::MainMenu) return next;

    float panW=280.f, panH=320.f;
    float panX=cx-panW*0.5f, panY=sh*0.38f;
    drawPanel(dl, panX, panY, panW, panH, _panelSlide);

    float btnW=panW-40.f, btnH=44.f, gap=10.f, startY=panY+20.f;

    struct Btn { const char* label; GameState target; };
    static const Btn BTNS[] = {
        {"SINGLEPLAYER",      GameState::WorldSelect},
        {"MULTIPLAYER",       GameState::Multiplayer},
        {"SETTINGS",          GameState::Settings},
    };
    for (int i=0;i<3;i++) {
        bool hov=false;
        float y=startY+i*(btnH+gap);
        if (menuButton(dl,BTNS[i].label,cx,y,btnW,btnH,false,hov)) {
            _panelSlide=1.f;
            return BTNS[i].target;
        }
        if (hov) _hoveredBtn=i;
    }
    {
        bool hov=false;
        float y=startY+3*(btnH+gap)+16.f;
        if (menuButton(dl,"QUIT",cx,y,btnW*0.5f,btnH*0.75f,false,hov)) {
            pendingServerIP="__QUIT__";
            return GameState::Connecting;
        }
    }
    dl->AddText(ImGui::GetFont(),12.f,{8.f,(float)sh-20.f},
                IM_COL32(50,60,70,160),"v0.1.0-alpha");
    return GameState::MainMenu;
}

// ── Account Page ──────────────────────────────────────────────────────────────
GameState MainMenu::drawAccount(ImDrawList* dl, float cx, float cy,
                                 int sw, int sh, float dt) {
    ImFont* font = ImGui::GetFont();

    if (_account.loggedIn) {
        // ── Signed-in view: show account details ──────────────────────────
        float panW=420.f, panH=280.f;
        float panX=cx-panW*0.5f, panY=cy-panH*0.5f;
        drawPanel(dl,panX,panY,panW,panH,_panelSlide);

        ImVec2 hsz=font->CalcTextSizeA(26.f,999.f,0.f,"ACCOUNT");
        dl->AddText(font,26.f,{cx-hsz.x*0.5f,panY+14.f},COL_WHITE,"ACCOUNT");
        dl->AddLine({panX+20,panY+48.f},{panX+panW-20,panY+48.f},COL_GREY_DIM,1.f);

        float lx=panX+30.f, cy2=panY+64.f, rowH=30.f;

        // Status dot + username
        dl->AddCircleFilled({lx+6, cy2+8}, 5.f, COL_GREEN, 8);
        dl->AddText(font, 18.f, {lx+20, cy2}, COL_WHITE, "Signed in as:");
        cy2 += 24.f;
        dl->AddText(font, 22.f, {lx+20, cy2}, COL_AMBER, _account.username);
        cy2 += rowH + 10.f;

        drawSectionHeader(dl,font,"SESSION",lx,cy2,panW-60.f); cy2+=22.f;

        // Show truncated token
        char tokenPreview[48] = {};
        int tokLen = (int)strlen(_account.sessionToken);
        if (tokLen > 30) {
            snprintf(tokenPreview, sizeof(tokenPreview), "%.12s...%.12s",
                     _account.sessionToken, _account.sessionToken + tokLen - 12);
        } else {
            snprintf(tokenPreview, sizeof(tokenPreview), "%s", _account.sessionToken);
        }
        dl->AddText(font, 12.f, {lx, cy2+2}, COL_WHITE_DIM, "Token:");
        dl->AddText(font, 12.f, {lx+50, cy2+2}, COL_TEAL, tokenPreview);
        cy2 += rowH;

        float bbY=panY+panH-54.f;
        bool hBack=false, hLogout=false;
        if (menuButton(dl,"BACK",panX+80.f+50.f,bbY,100.f,36.f,false,hBack)) {
            _panelSlide=1.f; return GameState::MainMenu;
        }
        if (menuButton(dl,"SIGN OUT",panX+panW-160.f+80.f,bbY,140.f,36.f,false,hLogout)) {
            _account.loggedIn = false;
            _account.username[0] = 0;
            _account.sessionToken[0] = 0;
            _account.uid[0] = 0;
            _accStatusMsg[0] = 0;
        }
        return GameState::Account;
    }

    // ── Not signed in: Login / Register ───────────────────────────────────
    float panW=480.f, panH=380.f;
    float panX=cx-panW*0.5f, panY=cy-panH*0.5f;
    drawPanel(dl,panX,panY,panW,panH,_panelSlide);

    const char* hdr=_accLoginMode?"SIGN IN":"CREATE ACCOUNT";
    ImVec2 hsz=font->CalcTextSizeA(26.f,999.f,0.f,hdr);
    dl->AddText(font,26.f,{cx-hsz.x*0.5f,panY+14.f},COL_WHITE,hdr);
    dl->AddLine({panX+20,panY+50.f},{panX+panW-20,panY+50.f},COL_GREY_DIM,1.f);

    float lx=panX+30.f, cy2=panY+64.f, rowH=34.f;

    drawTextInput(dl,"Username",lx,cy2,200.f,_accUser,sizeof(_accUser)); cy2+=rowH;
    drawTextInput(dl,"Password",lx,cy2,200.f,_accPass,sizeof(_accPass),true); cy2+=rowH;

    if (!_accLoginMode) {
        drawTextInput(dl,"Confirm Pass",lx,cy2,200.f,_accPass2,sizeof(_accPass2),true); cy2+=rowH;
    }

    cy2 += 8.f;

    if (_accStatusMsg[0]) {
        ImVec2 ssz=font->CalcTextSizeA(13.f,999.f,0.f,_accStatusMsg);
        dl->AddText(font,13.f,{cx-ssz.x*0.5f,cy2},_accStatusIsError?COL_RED:COL_GREEN,_accStatusMsg);
        cy2+=20.f;
    }

    {
        const char* toggleLbl=_accLoginMode?"No account? Register":"Have account? Sign In";
        ImVec2 tsz2=font->CalcTextSizeA(13.f,999.f,0.f,toggleLbl);
        dl->AddText(font,13.f,{cx-tsz2.x*0.5f,cy2},colAlpha(COL_TEAL,0.8f),toggleLbl);
        ImGui::SetCursorScreenPos({cx-tsz2.x*0.5f,cy2});
        ImGui::PushID("toggle_acc_mode");
        if (ImGui::InvisibleButton("##tm",{tsz2.x,14.f})) { _accLoginMode=!_accLoginMode; _accStatusMsg[0]=0; }
        ImGui::PopID();
    }

    float bbY=panY+panH-54.f;
    bool hBack=false, hAction=false;
    if (menuButton(dl,"BACK",panX+60.f+50.f,bbY,100.f,36.f,false,hBack)) {
        _panelSlide=1.f; return GameState::MainMenu;
    }
    const char* actionLbl=_accLoginMode?"SIGN IN":"REGISTER";
    if (menuButton(dl,actionLbl,panX+panW-160.f+80.f,bbY,140.f,36.f,false,hAction)) {
        if (_accUser[0]==0) { snprintf(_accStatusMsg,sizeof(_accStatusMsg),"Username required."); _accStatusIsError=true; }
        else if (_accPass[0]==0) { snprintf(_accStatusMsg,sizeof(_accStatusMsg),"Password required."); _accStatusIsError=true; }
        else if (!_accLoginMode && strcmp(_accPass, _accPass2)!=0) {
            snprintf(_accStatusMsg,sizeof(_accStatusMsg),"Passwords do not match."); _accStatusIsError=true;
        } else {
            // TODO: HTTP request to auth server
            // For now, fake login for testing
            _account.loggedIn = true;
            snprintf(_account.username, sizeof(_account.username), "%s", _accUser);
            snprintf(_account.sessionToken, sizeof(_account.sessionToken), "tok_%s_%d", _accUser, (int)_time);
            snprintf(_accStatusMsg,sizeof(_accStatusMsg),"%s", _accLoginMode?"Signed in!":"Account created!");
            _accStatusIsError = false;
        }
    }
    return GameState::Account;
}

// ── Multiplayer (now just server connection, no login) ────────────────────────
GameState MainMenu::drawMultiplayer(ImDrawList* dl, float cx, float cy,
                                     int sw, int sh, float dt) {
    float panW=460.f, panH=320.f;
    float panX=cx-panW*0.5f, panY=cy-panH*0.5f;
    drawPanel(dl,panX,panY,panW,panH,_panelSlide);
    ImFont* font=ImGui::GetFont();

    ImVec2 hsz=font->CalcTextSizeA(26.f,999.f,0.f,"MULTIPLAYER");
    dl->AddText(font,26.f,{cx-hsz.x*0.5f,panY+14.f},COL_WHITE,"MULTIPLAYER");
    dl->AddLine({panX+20,panY+50.f},{panX+panW-20,panY+50.f},COL_GREY_DIM,1.f);

    float lx=panX+30.f, cy2=panY+64.f, rowH=34.f;

    // Account status indicator
    if (_account.loggedIn) {
        dl->AddCircleFilled({lx+6, cy2+8}, 4.f, COL_GREEN, 8);
        char accTxt[128];
        snprintf(accTxt, sizeof(accTxt), "Signed in as %s", _account.username);
        dl->AddText(font, 14.f, {lx+16, cy2+2}, COL_GREEN, accTxt);
    } else {
        dl->AddCircleFilled({lx+6, cy2+8}, 4.f, COL_AMBER, 8);
        dl->AddText(font, 14.f, {lx+16, cy2+2}, COL_AMBER, "Not signed in — connect as guest");
    }
    cy2 += rowH;

    drawSectionHeader(dl,font,"SERVER",lx,cy2,panW-60.f); cy2+=22.f;
    drawTextInput(dl,"Server IP",lx,cy2,200.f,_serverInput,sizeof(_serverInput)); cy2+=rowH;
    drawInputInt(dl,"Port",lx,cy2,panW-60.f,_portInput,1024,65535); cy2+=rowH+8.f;

    float bbY=panY+panH-54.f;
    bool hBack=false, hConn=false;
    if (menuButton(dl,"BACK",panX+60.f+50.f,bbY,100.f,36.f,false,hBack)) {
        _panelSlide=1.f; return GameState::MainMenu;
    }
    if (menuButton(dl,"CONNECT",panX+panW-160.f+80.f,bbY,140.f,36.f,false,hConn)) {
        snprintf(pendingUsername,sizeof(pendingUsername),"%s",
                 _account.loggedIn ? _account.username : "Guest");
        snprintf(_settings.lastServer,sizeof(_settings.lastServer),"%s",_serverInput);
        _settings.serverPort=_portInput;
        pendingServerIP=_serverInput;
        pendingServerPort=_portInput;
        _connectTimer=0.f; _connectFailed=false;
        return GameState::Connecting;
    }
    return GameState::Multiplayer;
}

GameState MainMenu::drawSettings(ImDrawList* dl, float cx, float cy,
                                  int sw, int sh, float dt) {
    float panW=540.f, panH=440.f;
    float panX=cx-panW*0.5f, panY=cy-panH*0.5f;
    drawPanel(dl,panX,panY,panW,panH,_panelSlide);
    ImFont* font=ImGui::GetFont();

    ImVec2 hsz=font->CalcTextSizeA(28.f,999.f,0.f,"SETTINGS");
    dl->AddText(font,28.f,{cx-hsz.x*0.5f,panY+14.f},COL_WHITE,"SETTINGS");
    dl->AddLine({panX+20,panY+50.f},{panX+panW-20,panY+50.f},COL_GREY_DIM,1.f);

    static const char* TABS[]={"GRAPHICS","AUDIO","NETWORK"};
    float tabW=(panW-40.f)/3.f, tabH=28.f, tabY=panY+56.f;
    for (int i=0;i<3;i++)
        if (drawTab(dl,font,TABS[i],panX+20.f+i*tabW,tabY,tabW,tabH,_settingsTab==i))
            _settingsTab=i;

    float cy2=panY+56.f+tabH+20.f, lx=panX+30.f, rowH=34.f;
    if (_settingsTab==0) {
        drawSectionHeader(dl,font,"RENDERING",lx,cy2,panW-60.f); cy2+=22.f;
        drawSlider(dl,"Render Distance",lx,cy2,panW-60.f,_settings.renderDistance,1.f,8.f,"%.0f chunks"); cy2+=rowH;
        drawSlider(dl,"Field of View",lx,cy2,panW-60.f,_settings.fovF,60.f,110.f,"%.0f"); cy2+=rowH;
        _settings.fov=(int)_settings.fovF;
        drawToggle(dl,"VSync",lx,cy2,_settings.vsync); cy2+=rowH+10.f;
        drawSectionHeader(dl,font,"INPUT",lx,cy2,panW-60.f); cy2+=22.f;
        drawSlider(dl,"Mouse Sensitivity",lx,cy2,panW-60.f,_settings.mouseSens,0.01f,0.5f,"%.3f");
    } else if (_settingsTab==1) {
        drawSectionHeader(dl,font,"VOLUME",lx,cy2,panW-60.f); cy2+=22.f;
        drawSlider(dl,"Master Volume",lx,cy2,panW-60.f,_settings.masterVolume,0.f,1.f,"%.0f%%"); cy2+=rowH;
        drawSlider(dl,"Music Volume",lx,cy2,panW-60.f,_settings.musicVolume,0.f,1.f,"%.0f%%"); cy2+=rowH;
        drawSlider(dl,"SFX Volume",lx,cy2,panW-60.f,_settings.sfxVolume,0.f,1.f,"%.0f%%");
    } else {
        drawSectionHeader(dl,font,"SERVER",lx,cy2,panW-60.f); cy2+=22.f;
        drawTextInput(dl,"Default Server IP",lx,cy2,200.f,_settings.lastServer,sizeof(_settings.lastServer)); cy2+=rowH;
        drawInputInt(dl,"Default Port",lx,cy2,panW-60.f,_settings.serverPort,1024,65535); cy2+=rowH;
        drawSectionHeader(dl,font,"AUTH",lx,cy2,panW-60.f); cy2+=22.f;
        drawInputInt(dl,"Auth Server Port",lx,cy2,panW-60.f,_settings.authPort,1024,65535);
    }

    float bbY=panY+panH-54.f;
    bool hBack=false, hApply=false;
    if (menuButton(dl,"BACK",panX+80.f+50.f,bbY,100.f,36.f,false,hBack)) {
        _settings.save(); _panelSlide=1.f; return GameState::MainMenu;
    }
    if (menuButton(dl,"APPLY & SAVE",panX+panW-180.f+80.f,bbY,160.f,36.f,false,hApply))
        _settings.save();
    return GameState::Settings;
}

GameState MainMenu::drawWorldSel(ImDrawList* dl, float cx, float cy,
                                  int sw, int sh, float dt) {
    float panW=500.f, panH=380.f;
    float panX=cx-panW*0.5f, panY=cy-panH*0.5f;
    drawPanel(dl,panX,panY,panW,panH,_panelSlide);
    ImFont* font=ImGui::GetFont();

    ImVec2 hsz=font->CalcTextSizeA(26.f,999.f,0.f,"SELECT WORLD");
    dl->AddText(font,26.f,{cx-hsz.x*0.5f,panY+14.f},COL_WHITE,"SELECT WORLD");
    dl->AddLine({panX+20,panY+48.f},{panX+panW-20,panY+48.f},COL_GREY_DIM,1.f);

    static const char* WORLDS[]={"World 1","World 2","World 3"};
    static const char* SUBTXT[]={"Last played: Today","Last played: Yesterday","New World"};
    float wy=panY+62.f;
    for (int i=0;i<3;i++) {
        float rx=panX+20.f, rw=panW-40.f, rh=54.f;
        bool hover=ImGui::IsMouseHoveringRect({rx,wy},{rx+rw,wy+rh});
        dl->AddRectFilled({rx,wy},{rx+rw,wy+rh},hover?COL_BTN_HOVER:COL_BTN_NORM,3.f);
        dl->AddRect({rx,wy},{rx+rw,wy+rh},hover?COL_TEAL_DIM:COL_PANEL_EDGE,3.f,0,1.f);
        dl->AddText(font,18.f,{rx+14,wy+8},COL_WHITE,WORLDS[i]);
        dl->AddText(font,13.f,{rx+14,wy+30},COL_WHITE_DIM,SUBTXT[i]);
        ImGui::SetCursorScreenPos({rx,wy});
        ImGui::PushID(i+50);
        if (ImGui::InvisibleButton("##world",{rw,rh})) {
            pendingServerIP=_settings.lastServer;
            pendingServerPort=_settings.serverPort;
            ImGui::PopID();
            return GameState::Connecting;
        }
        ImGui::PopID();
        wy+=rh+8.f;
    }
    bool hBack=false;
    if (menuButton(dl,"BACK",cx,panY+panH-52.f,140.f,36.f,false,hBack)) {
        _panelSlide=1.f; return GameState::MainMenu;
    }
    return GameState::WorldSelect;
}

GameState MainMenu::drawConnecting(ImDrawList* dl, float cx, float cy,
                                    int sw, int sh, float dt) {
    _connectTimer+=dt;
    float panW=380.f, panH=180.f;
    float panX=cx-panW*0.5f, panY=cy-panH*0.5f;
    drawPanel(dl,panX,panY,panW,panH);
    ImFont* font=ImGui::GetFont();

    if (!_connectFailed) {
        char dots[8]="";
        int d=(int)(_connectTimer*2.f)%4;
        for (int i=0;i<d;i++) dots[i]='.'; dots[d]=0;
        char msg[128];
        snprintf(msg,sizeof(msg),"Connecting to %s:%d%s",pendingServerIP.c_str(),pendingServerPort,dots);
        ImVec2 msz=font->CalcTextSizeA(18.f,999.f,0.f,msg);
        dl->AddText(font,18.f,{cx-msz.x*0.5f,cy-10.f},COL_WHITE,msg);
        if (_connectTimer>8.f) _connectFailed=true;
    } else {
        const char* msg="Connection failed.";
        ImVec2 msz=font->CalcTextSizeA(18.f,999.f,0.f,msg);
        dl->AddText(font,18.f,{cx-msz.x*0.5f,cy-20.f},COL_RED,msg);
        bool hRetry=false, hBack=false;
        if (menuButton(dl,"RETRY",cx-45.f,cy+10.f,80.f,32.f,false,hRetry)) {
            _connectTimer=0.f; _connectFailed=false; return GameState::Connecting;
        }
        if (menuButton(dl,"BACK",cx+45.f,cy+10.f,80.f,32.f,false,hBack)) {
            pendingServerIP=""; return GameState::Multiplayer;
        }
    }
    return GameState::Connecting;
}

// ── Main draw ─────────────────────────────────────────────────────────────────
GameState MainMenu::draw(float dt, int screenW, int screenH) {
    _time+=dt;
    _panelSlide+=((0.f)-_panelSlide)*std::min(8.f*dt,1.f);
    tickParticles(dt,screenW,screenH);

    ImDrawList* bgDl = ImGui::GetBackgroundDrawList();
    drawBackground(bgDl, _time, screenW, screenH);
    drawParticles(bgDl, screenW, screenH);

    ImDrawList* dl = beginFullscreenWindow(screenW, screenH);

    float cx=screenW*0.5f, cy=screenH*0.5f;

    GameState next=_state;
    switch (_state) {
    case GameState::MainMenu:   next=drawMainPage(dl,cx,cy,screenW,screenH,dt); break;
    case GameState::Settings:   next=drawSettings(dl,cx,cy,screenW,screenH,dt); break;
    case GameState::WorldSelect:next=drawWorldSel(dl,cx,cy,screenW,screenH,dt); break;
    case GameState::Multiplayer:next=drawMultiplayer(dl,cx,cy,screenW,screenH,dt); break;
    case GameState::Account:    next=drawAccount(dl,cx,cy,screenW,screenH,dt); break;
    case GameState::Connecting: next=drawConnecting(dl,cx,cy,screenW,screenH,dt); break;
    default: break;
    }

    endFullscreenWindow();

    if (next!=_state) {
        _state=next; _hoveredBtn=-1; _panelSlide=1.f;
    }
    return _state;
}
