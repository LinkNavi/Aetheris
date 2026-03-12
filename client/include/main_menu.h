#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <array>
#include <cmath>

// ── Game state machine ────────────────────────────────────────────────────────
enum class GameState {
    MainMenu,
    Settings,
    WorldSelect,
    Multiplayer,   // IP/port + login panel
    Connecting,
    InGame,
};

// ── Settings ──────────────────────────────────────────────────────────────────
struct GameSettings {
    float renderDistance = 2.f;
    bool  vsync          = true;
    int   fov            = 70;
    float fovF           = 70.f;  // float mirror — slider works on this
    float mouseSens      = 0.10f;

    float masterVolume   = 1.0f;
    float musicVolume    = 0.6f;
    float sfxVolume      = 0.8f;

    char  lastServer[128] = "127.0.0.1";
    int   serverPort      = 7777;

    void save(const char* path = "settings.cfg") const;
    void load(const char* path = "settings.cfg");
};

// ── World entry (scanned from disk) ──────────────────────────────────────────
struct WorldEntry {
    std::string name;        // folder name
    std::string path;        // full path
    std::string lastPlayed;  // from world meta or "Unknown"
};

// ── Particle ──────────────────────────────────────────────────────────────────
struct MenuParticle {
    float x, y, vx, vy, life, size, brightness;
};

// ── MainMenu ──────────────────────────────────────────────────────────────────
class MainMenu {
public:
    MainMenu();

    // Call every frame inside ImGui frame. Returns current GameState.
    // When it returns Connecting, read pendingServerIP / pendingServerPort.
    GameState draw(float dt, int screenW, int screenH);

    GameSettings& settings() { return _settings; }

    std::string pendingServerIP;
    int         pendingServerPort = 7777;

    // Auth — filled when user submits login (even if offline/stub for now)
    char pendingUsername[64] = {};

private:
    GameState    _state = GameState::MainMenu;
    GameSettings _settings;

    // ── Particles ─────────────────────────────────────────────────────────────
    static constexpr int MAX_PARTICLES = 80;
    MenuParticle _particles[MAX_PARTICLES]{};
    uint32_t     _rngState = 0xdeadbeef;
    float        _rngFloat(float mn, float mx);
    void         spawnParticle(int i);
    void         tickParticles(float dt);
    void         drawParticles(ImDrawList* dl, int sw, int sh);

    // ── Pages ─────────────────────────────────────────────────────────────────
    GameState drawMainPage  (ImDrawList* dl, float cx, float cy, int sw, int sh, float dt);
    GameState drawSettings  (ImDrawList* dl, float cx, float cy, int sw, int sh);
    GameState drawWorldSel  (ImDrawList* dl, float cx, float cy, int sw, int sh);
    GameState drawMultiplayer(ImDrawList* dl, float cx, float cy, int sw, int sh);

    // ── Custom widgets ────────────────────────────────────────────────────────
    bool menuButton (ImDrawList* dl, const char* label, float cx, float y,
                     float w, float h, int btnId);
    void drawSlider (ImDrawList* dl, const char* label, float x, float y,
                     float w, float& val, float mn, float mx, const char* fmt = "%.2f");
    void drawToggle (ImDrawList* dl, const char* label, float x, float y, bool& val);
    void drawInputInt(ImDrawList* dl, const char* label, float x, float y,
                      float w, int& val, int mn, int mx);
    // Styled InputText — draws the box then places a transparent InputText over it
    void drawTextInput(ImDrawList* dl, const char* label, float x, float y,
                       float fieldW, char* buf, int bufSz,
                       bool password = false);

    // ── State ─────────────────────────────────────────────────────────────────
    float _time       = 0.f;
    int   _hoveredBtn = -1;   // reset each frame; set by menuButton when hovered
    int   _settingsTab = 0;

    // Multiplayer / login fields
    char  _serverInput[128] = "127.0.0.1";
    int   _portInput        = 7777;
    char  _username[64]     = {};
    char  _password[64]     = {};
    bool  _loginMode        = true;  // true=Login, false=Register
    char  _statusMsg[128]   = {};    // e.g. "Invalid password"
    bool  _statusIsError    = false;

    // World list
    std::vector<WorldEntry> _worlds;
    float _worldScanTimer = 0.f;    // rescan every 2s
    void  scanWorlds();

    // Panel slide animation
    float _panelSlide = 1.f;        // 1=offscreen, 0=fully in
};