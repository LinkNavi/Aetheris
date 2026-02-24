#pragma once

namespace Config {
    // How many chunks to load in each axis around the player.
    // e.g. 2 → loads a (2*2+1)^3 = 5x5x5 cube of chunks.
    // Change at will — server and client both read this.
    inline constexpr int CHUNK_RADIUS_XZ = 2; // horizontal
    inline constexpr int CHUNK_RADIUS_Y  = 1; // vertical

    inline constexpr int   SERVER_PORT    = 7777;
    inline constexpr float PLAYER_WIDTH   = 0.6f;
    inline constexpr float PLAYER_HEIGHT  = 1.8f;
inline constexpr float FRICTION     = 8.0f;
inline constexpr float GROUND_ACCEL = 15.0f;
inline constexpr float AIR_ACCEL    = 2.5f;
    inline constexpr float WALK_SPEED     = 8.0f;
    inline constexpr float SPRINT_MULT    = 1.8f;
    inline constexpr float JUMP_VEL       = 8.0f;
    inline constexpr float GRAVITY        = -22.0f;
    inline constexpr float MOUSE_SENS     = 0.1f;
}
