#include "water_sim.h"
#include <cmath>

WaterMesh buildWaterMesh(const WaterChunk& water, const ChunkData& terrain) {
    WaterMesh mesh;
    mesh.coord = water.coord;

    int S  = ChunkData::SIZE;
    int P  = ChunkData::PADDED;
    int ox = water.coord.x * S;
    int oy = water.coord.y * S;
    int oz = water.coord.z * S;

    for (int x = 0; x < S; x++)
    for (int z = 0; z < S; z++)
    for (int y = 0; y < S; y++) {
        uint8_t level = water.get(x, y, z);
        if (level == 0) continue;

        // ── Surface suppression ───────────────────────────────────────────
        // Skip submerged voxels — only the topmost water voxel in each
        // column gets a surface quad.
        bool waterAbove = (y + 1 < S) && (water.get(x, y + 1, z) > 0);
        if (waterAbove) continue;

        // Also suppress if terrain is solid directly above.
        // PADDED = SIZE+1 so y+1 is always a valid index.
        bool solidAbove = terrain.values[x][y + 1][z] < 0.f;
        if (solidAbove) continue;

        // ── Surface height ────────────────────────────────────────────────
        float worldY   = (float)(oy + y);
        float surfaceY = worldY + (float)level / (float)WATER_MAX;

        // ── Shore corner blending ─────────────────────────────────────────
        // At each corner check whether terrain is within 1 voxel of the
        // water surface. If yes we're at a shoreline and nudge that corner
        // down so water meets the ground. If terrain is further away we
        // leave the corner flat — this keeps open-water quads intact.
        auto shoreHeight = [&](int lx, int lz) -> float {
            if (lx < 0 || lx >= P || lz < 0 || lz >= P)
                return surfaceY;

            // Only scan 1 voxel down from water surface for shore detection
            for (int ly = y + 1; ly >= std::max(0, y - 1); ly--) {
                if (ly >= P) continue;
                float v = terrain.values[lx][ly][lz];
                if (v < 0.f) {
                    // Solid found — interpolate crossing height
                    float vAbove = (ly + 1 < P) ? terrain.values[lx][ly + 1][lz] : 0.f;
                    float denom  = vAbove - v;
                    float t      = (denom != 0.f) ? vAbove / denom : 0.f;
                    float crossY = (float)(oy + ly) + (1.f - t);
                    // Only apply blend if terrain is close to water surface
                    if (crossY >= surfaceY - 1.f)
                        return std::min(surfaceY, crossY);
                    // Terrain too far down — open water, stay flat
                    return surfaceY;
                }
            }
            return surfaceY;
        };

        float h00 = shoreHeight(x,     z    );
        float h10 = shoreHeight(x + 1, z    );
        float h01 = shoreHeight(x,     z + 1);
        float h11 = shoreHeight(x + 1, z + 1);

        // ── Depth ─────────────────────────────────────────────────────────
        int depth = 0;
        for (int dy = y; dy >= 0; dy--) {
            if (water.get(x, dy, z) > 0) depth++;
            else break;
        }
        float depthFrac = std::min((float)depth / 8.f, 1.f);

        // ── Flow direction ────────────────────────────────────────────────
        float flowAngle = 0.f;
        {
            float fdx = 0.f, fdz = 0.f;
            if (x > 0)   fdx -= (float)water.get(x - 1, y, z);
            if (x < S-1) fdx += (float)water.get(x + 1, y, z);
            if (z > 0)   fdz -= (float)water.get(x, y, z - 1);
            if (z < S-1) fdz += (float)water.get(x, y, z + 1);
            if (fdx != 0.f || fdz != 0.f)
                flowAngle = std::atan2(fdz, fdx);
        }

        // ── Build quad ────────────────────────────────────────────────────
        float wx0 = (float)(ox + x);
        float wz0 = (float)(oz + z);
        float wx1 = wx0 + 1.f;
        float wz1 = wz0 + 1.f;

        uint32_t base = (uint32_t)mesh.vertices.size();

        WaterVertex v0, v1, v2, v3;
        v0.pos = {wx0, h00, wz0}; v0.uv = {0,0}; v0.depth = depthFrac; v0.flow = flowAngle;
        v1.pos = {wx1, h10, wz0}; v1.uv = {1,0}; v1.depth = depthFrac; v1.flow = flowAngle;
        v2.pos = {wx1, h11, wz1}; v2.uv = {1,1}; v2.depth = depthFrac; v2.flow = flowAngle;
        v3.pos = {wx0, h01, wz1}; v3.uv = {0,1}; v3.depth = depthFrac; v3.flow = flowAngle;

        mesh.vertices.push_back(v0);
        mesh.vertices.push_back(v1);
        mesh.vertices.push_back(v2);
        mesh.vertices.push_back(v3);

        // Top face
        mesh.indices.push_back(base);     mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base);     mesh.indices.push_back(base + 2); mesh.indices.push_back(base + 3);
        // Bottom face (visible from below water)
        mesh.indices.push_back(base + 2); mesh.indices.push_back(base + 1); mesh.indices.push_back(base);
        mesh.indices.push_back(base + 3); mesh.indices.push_back(base + 2); mesh.indices.push_back(base);
    }

    return mesh;
}
