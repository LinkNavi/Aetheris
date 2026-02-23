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

// Scalar field — server fills this, shared code meshes it
struct ChunkData {
    ChunkCoord coord;
    static constexpr int SIZE = 32;
    float values[SIZE][SIZE][SIZE];
};
