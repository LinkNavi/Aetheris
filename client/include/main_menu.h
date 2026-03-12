#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include "http_client.h"

enum class GameState {
    MainMenu,
    Settings,
    WorldSelect,
    Multiplayer,
    Account,
    Connecting,
    InGame,
};

struct GameSettings {
    float renderDistance = 2.f;
    bool  vsync          = true;
    int   fov            = 70;
    float fovF           = 70.f;
    float mouseSens      = 0.10f;

    float masterVolume   = 1.0f;
    float musicVolume    = 0.6f;
    float sfxVolume      = 0.8f;

    char  lastServer[128] = "127.0.0.1";
    int   serverPort      = 7777;
    int   authPort        = 8080;

    void save(const char* path = "settings.cfg") const;
    void load(const char* path = "settings.cfg");
};

struct WorldEntry {
    std::string name;
    std::string path;
    std::string lastPlayed;
};

struct MenuParticle {
    float x, y, vx, vy, life, size, brightness;
};

// Persistent account state across screens
struct AccountState {
    bool  loggedIn = false;
    char  username[64] = {};
    char  sessionToken[512] = {};
    char  uid[128] = {};
};

class MainMenu {
public:
    MainMenu();

    // Returns current GameState. When Connecting, read pendingServerIP/Port.
    GameState draw(float dt, int screenW, int screenH);

    GameSettings& settings() { return _settings; }
    AccountState& account()  { return _account; }

    std::string pendingServerIP;
    int         pendingServerPort = 7777;
    char        pendingUsername[64] = {};

private:
    GameState    _state = GameState::MainMenu;
    GameSettings _settings;
    AccountState _account;

    static constexpr int MAX_PARTICLES = 80;
    MenuParticle _particles[MAX_PARTICLES]{};
    float        _time        = 0.f;
    int          _hoveredBtn  = -1;
    int          _settingsTab = 0;

    // Account / login
    char  _accUser[64]     = {};
    char  _accPass[64]     = {};
    char  _accPass2[64]    = {};
    bool  _accLoginMode    = true;
    char  _accStatusMsg[128] = {};
    bool  _accStatusIsError  = false;

    // Multiplayer (now just server connect, no login)
    char  _serverInput[128] = "127.0.0.1";
    int   _portInput        = 7777;

    // Connecting screen
    float _connectTimer     = 0.f;
    bool  _connectFailed    = false;

    std::vector<WorldEntry> _worlds;
    float _worldScanTimer = 0.f;
    void  scanWorlds();

    float _panelSlide = 1.f;

    void spawnParticle(int i, int sw, int sh);
    void tickParticles(float dt, int sw, int sh);
    void drawParticles(ImDrawList* dl, int sw, int sh);

    GameState drawMainPage   (ImDrawList* dl, float cx, float cy, int sw, int sh, float dt);
    GameState drawSettings   (ImDrawList* dl, float cx, float cy, int sw, int sh, float dt);
    GameState drawWorldSel   (ImDrawList* dl, float cx, float cy, int sw, int sh, float dt);
    GameState drawMultiplayer(ImDrawList* dl, float cx, float cy, int sw, int sh, float dt);
    GameState drawAccount    (ImDrawList* dl, float cx, float cy, int sw, int sh, float dt);
    GameState drawConnecting (ImDrawList* dl, float cx, float cy, int sw, int sh, float dt);

    // Account button drawn on main page (top-right)
    void drawAccountButton(ImDrawList* dl, int sw, int sh, GameState& next);

    bool menuButton(ImDrawList* dl, const char* label, float cx, float y,
                    float w, float h, bool isHovered, bool& outHovered);
    void drawSlider   (ImDrawList* dl, const char* label, float x, float y,
                       float w, float& val, float mn, float mx, const char* fmt = "%.2f");
    void drawToggle   (ImDrawList* dl, const char* label, float x, float y, bool& val);
    void drawInputInt (ImDrawList* dl, const char* label, float x, float y,
                       float w, int& val, int mn, int mx);
    void drawTextInput(ImDrawList* dl, const char* label, float x, float y,
                       float fieldW, char* buf, int bufSz, bool password = false);
};
