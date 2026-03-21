#pragma once
#include <array>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include "chunk.h"

// Water level per voxel: 0 = empty, 1-8 = water amount (8 = full)
static constexpr int WATER_MAX   = 8;
static constexpr int WATER_EMPTY = 0;

struct WaterChunk {
    ChunkCoord coord;
    static constexpr int SIZE = ChunkData::SIZE;
    uint8_t levels[SIZE][SIZE][SIZE]{};

    uint8_t  get(int x, int y, int z) const { return levels[x][y][z]; }
    void     set(int x, int y, int z, uint8_t v) { levels[x][y][z] = v; }
    bool     empty() const {
        for (int x=0;x<SIZE;x++) for (int y=0;y<SIZE;y++) for (int z=0;z<SIZE;z++)
            if (levels[x][y][z]) return false;
        return true;
    }
};

// Per-chunk water mesh — now produced by marching cubes
struct WaterVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    float     depth;    // 0=shallow, 1=deep — for color tinting
    float     flow;     // flow direction encoded as angle
};

struct WaterMesh {
    ChunkCoord              coord;
    std::vector<WaterVertex> vertices;
    std::vector<uint32_t>   indices;
};

// Build water mesh using marching cubes on the water density field.
// Water density = (waterSurfaceY - wy), clamped to zero where terrain is solid.
// This guarantees the water shoreline exactly matches the terrain mesh.
WaterMesh buildWaterMesh(const WaterChunk& water, const ChunkData& terrain);

// Get the water surface Y for a given world XZ position (biome-aware).
// Defined in noise_gen.cpp alongside terrain generation.
float getWaterSurfaceY(float wx, float wz);
