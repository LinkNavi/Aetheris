#pragma once
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

// Material IDs — match atlas column order (each = 0.25 of atlas width)
enum class BlockMat : uint8_t {
    Stone = 0,
    Dirt  = 1,
    Grass = 2,
    Sand  = 3,
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    uint32_t  material; // 0=stone, 1=dirt, 2=grass, 3=sand
};

struct ChunkCoord {
    int x, y, z;
    bool operator==(const ChunkCoord&) const = default;
};

struct ChunkMesh {
    ChunkCoord coord;
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

#include <functional>
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h = 0;
        h ^= std::hash<int>{}(c.x) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(c.y) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(c.z) + 0x9e3779b9 + (h<<6) + (h>>2);
        return h;
    }
};

struct ChunkData {
    ChunkCoord coord;
    static constexpr int SIZE   = 32;
    static constexpr int PADDED = SIZE + 1;
    float   values   [PADDED][PADDED][PADDED];
    uint8_t materials[PADDED][PADDED][PADDED]; // BlockMat per voxel
};
