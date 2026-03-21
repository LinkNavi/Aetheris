#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include "chunk.h"

// ── Tree voxel ────────────────────────────────────────────────────────────────
struct TreeVoxel {
    int     dx, dy, dz;   // offset from trunk base (world space)
    float   density;      // negative = solid (marching cubes convention)
    uint8_t material;     // BlockMat value
    float   noiseOff;     // per-voxel noise seed offset for texture variation
};

// ── Tree template — one variation of tree shape ───────────────────────────────
struct TreeTemplate {
    std::vector<TreeVoxel> voxels;
    int   trunkHeight  = 5;
    float canopyRadius = 3.f;
    float canopyYOff   = 0.f;  // canopy center offset from trunk top
};

// ── Placed tree instance ──────────────────────────────────────────────────────
// Stored per-chunk for health tracking and fall animation
struct TreeInstance {
    int     templateIdx  = 0;
    float   wx, wy, wz;
    float   health       = 100.f;
    float   maxHealth    = 100.f;
    bool    dead         = false;
    float   fallAngle    = 0.f;
    float   fallDir      = 0.f;
    bool    falling      = false;
    bool    fallen       = false;
    float   hitCooldown  = 0.f;  // add this
};

// ── Tree library — generated once from world seed ─────────────────────────────
class TreeLibrary {
public:
    static constexpr int COUNT = 12;

    void generate(int64_t seed);

    // Returns template index to use at this world position, or -1 for no tree
    int samplePlacement(float wx, float wy, float wz, float surfaceY) const;

    // Stamp a tree template into chunk density data
    void stamp(ChunkData& data, const TreeTemplate& tmpl,
               float wx, float wy, float wz) const;

    // Get template by index
    const TreeTemplate& get(int idx) const { return _templates[idx]; }

private:
    std::array<TreeTemplate, COUNT> _templates;
    int64_t _seed = 0;

    void buildTrunk(TreeTemplate& t, int height, float trunkRadius,
                    int64_t seed) const;
    void buildCanopy(TreeTemplate& t, float radius, int trunkHeight,
                     float yOff, int64_t seed) const;

    // Simple hash noise used for placement and texture
    static float hashf(int64_t seed, int x, int z);
    static float hashf3(int64_t seed, float x, float y, float z);
};

// ── Tree math — bark/leaf texture via noise (used in marching cubes UV) ───────
// Returns a 0-1 noise value for a given world position and material type.
// Used to add visual variation without a texture atlas entry per tree.
inline float treeBarkNoise(float wx, float wy, float wz) {
    // Simple fBm — rings + vertical streaks
    float ring  = std::sin(std::sqrt(wx*wx + wz*wz) * 1.8f + wy * 0.3f) * 0.5f + 0.5f;
    float grain = std::sin(wy * 4.2f + wx * 0.7f) * 0.5f + 0.5f;
    return ring * 0.6f + grain * 0.4f;
}

inline float treeLeafNoise(float wx, float wy, float wz) {
    // Clumpy blobs
    float a = std::sin(wx * 2.1f) * std::cos(wz * 1.9f) * std::sin(wy * 2.4f);
    float b = std::cos(wx * 3.3f + 0.5f) * std::sin(wz * 3.1f) * 0.5f;
    return (a + b) * 0.5f + 0.5f;
}
