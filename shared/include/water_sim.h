#pragma once
#include <array>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <cmath>
#include "chunk.h"

// Water level per voxel: 0 = empty, 1-7 = flowing, 8 = source
static constexpr int WATER_MAX   = 8;
static constexpr int WATER_EMPTY = 0;

struct WaterChunk {
    ChunkCoord coord;
    static constexpr int SIZE = ChunkData::SIZE;
    uint8_t levels[SIZE][SIZE][SIZE]{};

    uint8_t get(int x, int y, int z) const {
        if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE) return 0;
        return levels[x][y][z];
    }
    void set(int x, int y, int z, uint8_t v) {
        if (x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE)
            levels[x][y][z] = v;
    }
    bool empty() const {
        for (int x = 0; x < SIZE; x++)
            for (int y = 0; y < SIZE; y++)
                for (int z = 0; z < SIZE; z++)
                    if (levels[x][y][z]) return false;
        return true;
    }
};

struct WaterVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    float     depth;
    float     flow;
};

struct WaterMesh {
    ChunkCoord               coord;
    std::vector<WaterVertex> vertices;
    std::vector<uint32_t>    indices;
};

// Build water mesh — generates quad faces for each water voxel.
// Top face height depends on water level (level/8).
WaterMesh buildWaterMesh(const WaterChunk& water, const ChunkData& terrain);

// Biome-aware water surface Y (for initial sea fill). Defined in noise_gen.cpp.
float getWaterSurfaceY(float wx, float wz);
