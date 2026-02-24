#pragma once
#include <vector>
#include <glm/vec3.hpp>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
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
// Scalar field — server fills this, shared code meshes it
struct ChunkData {
    ChunkCoord coord;
    static constexpr int SIZE = 32;
    float values[SIZE][SIZE][SIZE];
};
