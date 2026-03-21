#pragma once

namespace Config {
    inline constexpr int CHUNK_RADIUS_XZ = 4;   // was 2
    inline constexpr int CHUNK_RADIUS_Y  = 3;   // was 1 — world is much taller now

    inline constexpr int   SERVER_PORT    = 7777;
    inline constexpr int   WORLD_SEED    = 62342340;
    inline constexpr float PLAYER_WIDTH   = 0.6f;
    inline constexpr float PLAYER_HEIGHT  = 1.8f;

    // ── Skyrim-style movement ─────────────────────────────────────────────────
    inline constexpr float WALK_SPEED    = 5.5f;
    inline constexpr float SPRINT_MULT   = 1.85f;
    inline constexpr float JUMP_VEL      = 8.5f;
    inline constexpr float GRAVITY       = -28.0f;

    inline constexpr float GROUND_ACCEL  = 11.0f;
    inline constexpr float FRICTION      = 14.0f;
    inline constexpr float AIR_ACCEL     = 1.8f;

    inline float MOUSE_SENS     = 0.1f;
    inline constexpr float DAY_LENGTH_SECONDS = 1200.f;
}
