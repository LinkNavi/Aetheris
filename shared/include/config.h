#pragma once

namespace Config {
    inline constexpr int CHUNK_RADIUS_XZ = 2;
    inline constexpr int CHUNK_RADIUS_Y  = 1;


    inline constexpr int   SERVER_PORT    = 7777;
    inline constexpr int   WORLD_SEED    = 1273;
    inline constexpr float PLAYER_WIDTH   = 0.6f;
    inline constexpr float PLAYER_HEIGHT  = 1.8f;
 inline constexpr float FRICTION      = 8.0f;   // was 50.0f — slower stop
inline constexpr float GROUND_ACCEL  = 12.0f;  // was 5.0f — snappier start but not instant
inline constexpr float AIR_ACCEL     = 0.3f;   // was 0.5f — less air control
inline constexpr float WALK_SPEED    = 4.5f;   // was 5.0f — slightly slower
inline constexpr float SPRINT_MULT   = 1.6f;   // was 1.8f
inline constexpr float JUMP_VEL      = 6.0f;   // was 8.0f — shorter jump
inline constexpr float GRAVITY       = -20.0f; // was -52.0f — floatier, less quake-y
    inline constexpr float MOUSE_SENS     = 0.1f;

    inline constexpr float DAY_LENGTH_SECONDS = 1200.f; // 20 real minutes
}
