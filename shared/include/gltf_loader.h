#pragma once
#include <vector>
#include <string>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct GltfVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct GltfMesh {
    std::vector<GltfVertex> vertices;
    std::vector<uint32_t>   indices;
    std::string             name;
};

struct GltfModel {
    std::vector<GltfMesh> meshes;
    bool valid = false;
};

// Loads a .glb file. Returns invalid model on failure.
GltfModel loadGlb(const char* path);
