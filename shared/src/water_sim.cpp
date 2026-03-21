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

        // Check if there's water above — if so this voxel is submerged, skip surface
        bool waterAbove = (y + 1 < S) && (water.get(x, y+1, z) > 0);

        // Check terrain above is not solid
        bool solidAbove = false;
        if (y + 1 < P) solidAbove = terrain.values[x][y+1][z] < 0.f;

        if (waterAbove || solidAbove) continue;

        // Water surface height — partial fill lifts the surface
        float surfaceY = (float)(oy + y) + (float)level / (float)WATER_MAX;

        // World X/Z positions
        float wx0 = (float)(ox + x);
        float wz0 = (float)(oz + z);
        float wx1 = wx0 + 1.f;
        float wz1 = wz0 + 1.f;

        // Depth — how deep is the water column below this surface?
        int depth = 0;
        for (int dy = y; dy >= 0; dy--) {
            if (water.get(x, dy, z) > 0) depth++;
            else break;
        }
        float depthFrac = std::min((float)depth / 8.f, 1.f);

        // Flow direction — approximate from level gradient
        float flowAngle = 0.f;
        {
            float dx = 0.f, dz = 0.f;
            if (x > 0)   dx -= (float)water.get(x-1, y, z);
            if (x < S-1) dx += (float)water.get(x+1, y, z);
            if (z > 0)   dz -= (float)water.get(x, y, z-1);
            if (z < S-1) dz += (float)water.get(x, y, z+1);
            if (dx != 0.f || dz != 0.f)
                flowAngle = std::atan2(dz, dx);
        }

        // Build quad (4 verts, 2 tris)
        uint32_t base = (uint32_t)mesh.vertices.size();

        WaterVertex v0, v1, v2, v3;
        v0.pos = {wx0, surfaceY, wz0}; v0.uv = {0,0}; v0.depth = depthFrac; v0.flow = flowAngle;
        v1.pos = {wx1, surfaceY, wz0}; v1.uv = {1,0}; v1.depth = depthFrac; v1.flow = flowAngle;
        v2.pos = {wx1, surfaceY, wz1}; v2.uv = {1,1}; v2.depth = depthFrac; v2.flow = flowAngle;
        v3.pos = {wx0, surfaceY, wz1}; v3.uv = {0,1}; v3.depth = depthFrac; v3.flow = flowAngle;

        mesh.vertices.push_back(v0);
        mesh.vertices.push_back(v1);
        mesh.vertices.push_back(v2);
        mesh.vertices.push_back(v3);

        mesh.indices.push_back(base);   mesh.indices.push_back(base+1); mesh.indices.push_back(base+2);
        mesh.indices.push_back(base);   mesh.indices.push_back(base+2); mesh.indices.push_back(base+3);
        // Back face so it's visible from below too
        mesh.indices.push_back(base+2); mesh.indices.push_back(base+1); mesh.indices.push_back(base);
        mesh.indices.push_back(base+3); mesh.indices.push_back(base+2); mesh.indices.push_back(base);
    }

    return mesh;
}
