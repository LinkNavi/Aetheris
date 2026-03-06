#pragma once

namespace Config {
    inline constexpr int CHUNK_RADIUS_XZ = 2;
    inline constexpr int CHUNK_RADIUS_Y  = 1;

    inline constexpr int   SERVER_PORT    = 7777;
    inline constexpr int   WORLD_SEED    = 1273;
    inline constexpr float PLAYER_WIDTH   = 0.6f;
    inline constexpr float PLAYER_HEIGHT  = 1.8f;

    // ── Skyrim-style movement ─────────────────────────────────────────────────
    inline constexpr float WALK_SPEED    = 5.5f;
    inline constexpr float SPRINT_MULT   = 1.85f;
    inline constexpr float JUMP_VEL      = 8.5f;
    inline constexpr float GRAVITY       = -28.0f;

    // Ground accel: how fast you reach target speed (higher = snappier)
    // Skyrim ~10-12: responsive but not instant
    inline constexpr float GROUND_ACCEL  = 11.0f;

    // Friction / decel rate when no input (Skyrim stops fairly fast)
    inline constexpr float FRICTION      = 14.0f;

    // Air steering: very low, Skyrim has almost no air control
    inline constexpr float AIR_ACCEL     = 1.8f;

    inline constexpr float MOUSE_SENS     = 0.1f;
    inline constexpr float DAY_LENGTH_SECONDS = 1200.f;
}
