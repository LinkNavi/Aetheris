#pragma once

namespace Config {
    inline constexpr int CHUNK_RADIUS_XZ = 2;
    inline constexpr int CHUNK_RADIUS_Y  = 1;

    inline constexpr int   SERVER_PORT    = 7777;
    inline constexpr int   WORLD_SEED    = 1273;
    inline constexpr float PLAYER_WIDTH   = 0.6f;
    inline constexpr float PLAYER_HEIGHT  = 1.8f;

    // ── Kinetic movement ──────────────────────────────────────────────────────
    // These are direct speed values — no friction/accel ramps on the ground.
    inline constexpr float WALK_SPEED    = 5.5f;
    inline constexpr float SPRINT_MULT   = 1.7f;
    inline constexpr float JUMP_VEL      = 9.0f;
    inline constexpr float GRAVITY       = -28.0f; // stronger gravity = snappier arc

    // Air steering: how quickly you can redirect mid-air (0=none, 1=instant)
    // Low value keeps momentum but lets you nudge your trajectory
    inline constexpr float AIR_ACCEL     = 4.0f;

    // Unused by kinetic controller but kept for dodge/combat use
    inline constexpr float FRICTION      = 8.0f;
    inline constexpr float GROUND_ACCEL  = 12.0f;

    inline constexpr float MOUSE_SENS     = 0.1f;
    inline constexpr float DAY_LENGTH_SECONDS = 1200.f;
}
