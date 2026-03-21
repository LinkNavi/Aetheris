#include "water_sim.h"
#include "table.h"
#include <cmath>
#include <algorithm>
#include <glm/geometric.hpp>

float getWaterSurfaceY(float wx, float wz);

// Same snap as terrain — picks whichever corner is closer to iso
static glm::vec3 snapInterp(float iso, glm::vec3 p0, float v0, glm::vec3 p1, float v1) {
    return (std::abs(v0 - iso) < std::abs(v1 - iso)) ? p0 : p1;
}

WaterMesh buildWaterMesh(const WaterChunk& /*water*/, const ChunkData& terrain) {
    WaterMesh mesh;
    mesh.coord = terrain.coord;

    constexpr int N   = ChunkData::SIZE;
    constexpr int P   = ChunkData::PADDED;
    constexpr float iso = 0.0f;

    int ox = terrain.coord.x * N;
    int oy = terrain.coord.y * N;
    int oz = terrain.coord.z * N;

    // Track which voxels are solid terrain
    // (used to decide snap vs lerp per edge)
    bool solid[P][P][P];

    float wd[P][P][P];
    bool hasAnyWater = false;

    for (int x = 0; x < P; x++)
    for (int z = 0; z < P; z++) {
        float wx = (float)(ox + x);
        float wz = (float)(oz + z);
        float waterY = getWaterSurfaceY(wx, wz);

        for (int y = 0; y < P; y++) {
            float wy = (float)(oy + y);
            float td = terrain.values[x][y][z]; // negative = solid

            solid[x][y][z] = (td < 0.f);

            if (td < 0.f) {
                // Solid terrain — use negated terrain density so shore edges
                // have the same magnitude as terrain MC used
                wd[x][y][z] = -td;
            } else {
                float d = wy - waterY;
                wd[x][y][z] = d;
                if (d < 0.f) hasAnyWater = true;
            }
        }
    }

    if (!hasAnyWater) return mesh;

    for (int z = 0; z < N; z++)
    for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
        float vals[8];
        glm::vec3 pos[8];
        bool isSolid[8];

        for (int c = 0; c < 8; c++) {
            int cx = x + corners[c].x;
            int cy = y + corners[c].y;
            int cz = z + corners[c].z;
            vals[c]     = wd[cx][cy][cz];
            pos[c]      = glm::vec3((float)cx, (float)cy, (float)cz);
            isSolid[c]  = solid[cx][cy][cz];
        }

        int cubeIndex = 0;
        for (int c = 0; c < 8; c++)
            if (vals[c] < iso)
                cubeIndex |= (1 << c);

        if (edgeTable[cubeIndex] == 0) continue;

        glm::vec3 edgeVerts[12];
        for (int e = 0; e < 12; e++) {
            if (edgeTable[cubeIndex] & (1 << e)) {
                int a = edgePairs[e][0], b = edgePairs[e][1];

                // If either corner of this edge is solid terrain,
                // use snap interp to match terrain MC exactly.
                // Otherwise (pure water-to-air edge) use snap too
                // so everything is consistent and blocky.
                edgeVerts[e] = snapInterp(iso, pos[a], vals[a], pos[b], vals[b]);
            }
        }

        for (int t = 0; triTable[cubeIndex][t] != -1; t += 3) {
            int e0 = triTable[cubeIndex][t];
            int e1 = triTable[cubeIndex][t + 1];
            int e2 = triTable[cubeIndex][t + 2];

            glm::vec3 v0 = edgeVerts[e0];
            glm::vec3 v1 = edgeVerts[e1];
            glm::vec3 v2 = edgeVerts[e2];

            glm::vec3 cr = glm::cross(v1 - v0, v2 - v0);
            if (glm::dot(cr, cr) < 1e-10f) continue;

            glm::vec3 off((float)ox, (float)oy, (float)oz);
            glm::vec3 wv0 = v0 + off;
            glm::vec3 wv1 = v1 + off;
            glm::vec3 wv2 = v2 + off;

            auto calcDepth = [](glm::vec3 wp) -> float {
                float wY = getWaterSurfaceY(wp.x, wp.z);
                return std::clamp((wY - wp.y) / 8.f, 0.f, 1.f);
            };
            auto makeUV = [](glm::vec3 wp) -> glm::vec2 {
                return {wp.x * 0.1f, wp.z * 0.1f};
            };

            uint32_t base = (uint32_t)mesh.vertices.size();
            WaterVertex wv;
            wv.flow = 0.f;

            wv.pos = wv0; wv.uv = makeUV(wv0); wv.depth = calcDepth(wv0);
            mesh.vertices.push_back(wv);
            wv.pos = wv1; wv.uv = makeUV(wv1); wv.depth = calcDepth(wv1);
            mesh.vertices.push_back(wv);
            wv.pos = wv2; wv.uv = makeUV(wv2); wv.depth = calcDepth(wv2);
            mesh.vertices.push_back(wv);

            mesh.indices.push_back(base);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base);
        }
    }

    return mesh;
}
