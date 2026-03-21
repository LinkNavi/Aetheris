#include "water_sim.h"
#include <cmath>
#include <algorithm>

float getWaterSurfaceY(float wx, float wz);

static inline float waterHeight(uint8_t level) {
    if (level == 0) return 0.f;
    return (float)level / (float)WATER_MAX - 0.02f;
}

static inline bool isSolid(const ChunkData& terrain, int x, int y, int z) {
    constexpr int P = ChunkData::PADDED;
    if (x < 0 || x >= P || y < 0 || y >= P || z < 0 || z >= P) return false;
    return terrain.values[x][y][z] < 0.f;
}

// For out-of-bounds neighbors, check sea level to decide if water continues.
// Prevents false faces at chunk boundaries.
static inline uint8_t getNeighborWater(const WaterChunk& water, int x, int y, int z,
                                        float ox, float oy, float oz) {
    constexpr int N = ChunkData::SIZE;
    if (x >= 0 && x < N && y >= 0 && y < N && z >= 0 && z < N)
        return water.get(x, y, z);

    // Out of bounds — assume water continues if below sea level
    float wx = ox + (float)x;
    float wy = oy + (float)y;
    float wz = oz + (float)z;
    float waterY = getWaterSurfaceY(wx, wz);
    if (wy < waterY) return WATER_MAX;
    return 0;
}

WaterMesh buildWaterMesh(const WaterChunk& water, const ChunkData& terrain) {
    WaterMesh mesh;
    mesh.coord = water.coord;

    constexpr int N = ChunkData::SIZE;
    float ox = (float)(water.coord.x * N);
    float oy = (float)(water.coord.y * N);
    float oz = (float)(water.coord.z * N);

    auto addQuad = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3,
                       float depth, float flow) {
        uint32_t base = (uint32_t)mesh.vertices.size();
        auto makeV = [&](glm::vec3 p) -> WaterVertex {
            return {p, {p.x * 0.1f, p.z * 0.1f}, depth, flow};
        };
        mesh.vertices.push_back(makeV(v0));
        mesh.vertices.push_back(makeV(v1));
        mesh.vertices.push_back(makeV(v2));
        mesh.vertices.push_back(makeV(v3));
        // Front face
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
        // Back face
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 3);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base);
    };

    for (int x = 0; x < N; x++)
    for (int y = 0; y < N; y++)
    for (int z = 0; z < N; z++) {
        uint8_t level = water.get(x, y, z);
        if (level == 0) continue;
        if (isSolid(terrain, x, y, z)) continue;

        float wx = ox + (float)x;
        float wy = oy + (float)y;
        float wz = oz + (float)z;

        float topH = waterHeight(level);
        float depth = std::clamp((float)level / (float)WATER_MAX, 0.f, 1.f);

        // Top face
        {
            uint8_t aboveLevel = getNeighborWater(water, x, y + 1, z, ox, oy, oz);
            bool aboveSolid = isSolid(terrain, x, y + 1, z);
            if (!aboveSolid && aboveLevel == 0) {
                glm::vec3 v0 = {wx,       wy + topH, wz};
                glm::vec3 v1 = {wx + 1.f, wy + topH, wz};
                glm::vec3 v2 = {wx + 1.f, wy + topH, wz + 1.f};
                glm::vec3 v3 = {wx,       wy + topH, wz + 1.f};
                addQuad(v0, v1, v2, v3, depth, 0.f);
            }
        }

        // Bottom face
        {
            uint8_t belowLevel = getNeighborWater(water, x, y - 1, z, ox, oy, oz);
            bool belowSolid = isSolid(terrain, x, y - 1, z);
            if (!belowSolid && belowLevel == 0) {
                glm::vec3 v0 = {wx,       wy, wz};
                glm::vec3 v1 = {wx + 1.f, wy, wz};
                glm::vec3 v2 = {wx + 1.f, wy, wz + 1.f};
                glm::vec3 v3 = {wx,       wy, wz + 1.f};
                addQuad(v3, v2, v1, v0, depth, 0.f);
            }
        }

        // -X
        {
            uint8_t nLevel = getNeighborWater(water, x - 1, y, z, ox, oy, oz);
            bool nSolid = isSolid(terrain, x - 1, y, z);
            if (!nSolid && nLevel == 0) {
                glm::vec3 v0 = {wx, wy,         wz};
                glm::vec3 v1 = {wx, wy,         wz + 1.f};
                glm::vec3 v2 = {wx, wy + topH,  wz + 1.f};
                glm::vec3 v3 = {wx, wy + topH,  wz};
                addQuad(v0, v1, v2, v3, depth, 0.f);
            }
        }
        // +X
        {
            uint8_t nLevel = getNeighborWater(water, x + 1, y, z, ox, oy, oz);
            bool nSolid = isSolid(terrain, x + 1, y, z);
            if (!nSolid && nLevel == 0) {
                glm::vec3 v0 = {wx + 1.f, wy,         wz + 1.f};
                glm::vec3 v1 = {wx + 1.f, wy,         wz};
                glm::vec3 v2 = {wx + 1.f, wy + topH,  wz};
                glm::vec3 v3 = {wx + 1.f, wy + topH,  wz + 1.f};
                addQuad(v0, v1, v2, v3, depth, 0.f);
            }
        }
        // -Z
        {
            uint8_t nLevel = getNeighborWater(water, x, y, z - 1, ox, oy, oz);
            bool nSolid = isSolid(terrain, x, y, z - 1);
            if (!nSolid && nLevel == 0) {
                glm::vec3 v0 = {wx + 1.f, wy,         wz};
                glm::vec3 v1 = {wx,       wy,         wz};
                glm::vec3 v2 = {wx,       wy + topH,  wz};
                glm::vec3 v3 = {wx + 1.f, wy + topH,  wz};
                addQuad(v0, v1, v2, v3, depth, 0.f);
            }
        }
        // +Z
        {
            uint8_t nLevel = getNeighborWater(water, x, y, z + 1, ox, oy, oz);
            bool nSolid = isSolid(terrain, x, y, z + 1);
            if (!nSolid && nLevel == 0) {
                glm::vec3 v0 = {wx,       wy,         wz + 1.f};
                glm::vec3 v1 = {wx + 1.f, wy,         wz + 1.f};
                glm::vec3 v2 = {wx + 1.f, wy + topH,  wz + 1.f};
                glm::vec3 v3 = {wx,       wy + topH,  wz + 1.f};
                addQuad(v0, v1, v2, v3, depth, 0.f);
            }
        }
    }

    return mesh;
}
