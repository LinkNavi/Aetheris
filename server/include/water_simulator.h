#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cmath>
#include "water_sim.h"
#include "chunk.h"
#include <cmath>
#include <algorithm>
class WaterSimulator {
public:
    static constexpr float TICK_RATE = 0.25f;

    void addTerrain(const ChunkData& chunk) {
        _terrain[chunk.coord] = chunk;
        // Always check — cheap enough
        if (chunkCouldHaveWater(chunk.coord))
            _dirty.insert(chunk.coord);
    }

    void removeTerrain(ChunkCoord coord) {
        _terrain.erase(coord);
        _water.erase(coord);
        _dirty.erase(coord);
    }

    void onTerrainChanged(int wx, int wy, int wz) {
        ChunkCoord cc = worldToChunk(wx, wy, wz);
        _dirty.insert(cc);
        for (auto& n : neighbours(cc)) _dirty.insert(n);
    }

    void placeWater(int wx, int wy, int wz, uint8_t level = WATER_MAX) {
        onTerrainChanged(wx, wy, wz);
    }
    void removeWater(int wx, int wy, int wz) {
        onTerrainChanged(wx, wy, wz);
    }
    void onBlockRemoved(int wx, int wy, int wz) {
        onTerrainChanged(wx, wy, wz);
    }
    void onBlockPlaced(int wx, int wy, int wz) {
        onTerrainChanged(wx, wy, wz);
    }

    std::vector<ChunkCoord> tick() {
        std::vector<ChunkCoord> changed;
        if (_dirty.empty()) return changed;

        for (const auto& cc : _dirty) {
            auto terrainIt = _terrain.find(cc);
            if (terrainIt == _terrain.end()) continue;

            // Build WaterChunk for packet compatibility
            WaterChunk wc;
            wc.coord = cc;
            int S = ChunkData::SIZE;
            int ox = cc.x * S, oy = cc.y * S, oz = cc.z * S;
            bool anyWater = false;

            for (int x = 0; x < S; x++)
            for (int z = 0; z < S; z++) {
                float wx = (float)(ox + x);
                float wz = (float)(oz + z);
                float waterY = getWaterSurfaceY(wx, wz);

                for (int y = 0; y < S; y++) {
                    float wy = (float)(oy + y);
                    bool solid = terrainIt->second.values[x][y][z] < 0.f;
                    if (!solid && wy < waterY) {
                        wc.set(x, y, z, WATER_MAX);
                        anyWater = true;
                    } else {
                        wc.set(x, y, z, 0);
                    }
                }
            }

            if (anyWater) {
                _water[cc] = wc;
            } else {
                _water.erase(cc);
            }
            changed.push_back(cc);
        }

        _dirty.clear();
        return changed;
    }

    const WaterChunk* getWater(ChunkCoord cc) const {
        auto it = _water.find(cc);
        return it != _water.end() ? &it->second : nullptr;
    }

    const ChunkData* getTerrain(ChunkCoord cc) const {
        auto it = _terrain.find(cc);
        return it != _terrain.end() ? &it->second : nullptr;
    }

    bool hasWater(ChunkCoord cc) const {
        auto it = _water.find(cc);
        return it != _water.end() && !it->second.empty();
    }

private:
    std::unordered_map<ChunkCoord, ChunkData,  ChunkCoordHash> _terrain;
    std::unordered_map<ChunkCoord, WaterChunk, ChunkCoordHash> _water;
    std::unordered_set<ChunkCoord, ChunkCoordHash>             _dirty;

    static ChunkCoord worldToChunk(int wx, int wy, int wz) {
        int s = ChunkData::SIZE;
        return {
            (int)std::floor((float)wx / s),
            (int)std::floor((float)wy / s),
            (int)std::floor((float)wz / s)
        };
    }

    static std::array<ChunkCoord, 6> neighbours(ChunkCoord cc) {
        return {{
            {cc.x+1,cc.y,cc.z},{cc.x-1,cc.y,cc.z},
            {cc.x,cc.y+1,cc.z},{cc.x,cc.y-1,cc.z},
            {cc.x,cc.y,cc.z+1},{cc.x,cc.y,cc.z-1}
        }};
    }

    // Quick Y-range check: does this chunk's Y range overlap ANY possible
    // water surface? Much cheaper than checking every voxel.
    bool chunkCouldHaveWater(ChunkCoord cc) const {
        int S = ChunkData::SIZE;
        float chunkMinY = (float)(cc.y * S);
        float chunkMaxY = (float)(cc.y * S + S);

        // Sample water level at chunk corners
        int ox = cc.x * S, oz = cc.z * S;
        float w0 = getWaterSurfaceY((float)ox, (float)oz);
        float w1 = getWaterSurfaceY((float)(ox+S), (float)oz);
        float w2 = getWaterSurfaceY((float)ox, (float)(oz+S));
        float w3 = getWaterSurfaceY((float)(ox+S), (float)(oz+S));
        float maxWater = std::max({w0, w1, w2, w3});

        // Chunk has water if its Y range is at or below the water surface
        // (chunk entirely above water = no water, chunk below or crossing = yes)
        return chunkMinY < maxWater;
    }
};
