#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "water_sim.h"
#include "chunk.h"

class WaterSimulator {
public:
    static constexpr float TICK_RATE = 0.15f;

    void addTerrain(const ChunkData& chunk) {
        _terrain[chunk.coord] = chunk;
        fillSeaLevel(chunk.coord);
    }

    void removeTerrain(ChunkCoord coord) {
        _terrain.erase(coord);
        _water.erase(coord);
        _dirty.erase(coord);
    }

    void placeWater(int wx, int wy, int wz, uint8_t level = WATER_MAX) {
        setWaterWorld(wx, wy, wz, level);
        markDirtyAround(wx, wy, wz);
    }

    void removeWater(int wx, int wy, int wz) {
        setWaterWorld(wx, wy, wz, 0);
        markDirtyAround(wx, wy, wz);
    }

    void onBlockRemoved(int wx, int wy, int wz) {
        markDirtyAround(wx, wy, wz);
    }

    void onBlockPlaced(int wx, int wy, int wz) {
        removeWater(wx, wy, wz);
    }

    void onTerrainChanged(int wx, int wy, int wz) {
        markDirtyAround(wx, wy, wz);
    }

    // Run one flow tick. Returns chunks whose water changed.
    std::vector<ChunkCoord> tick() {
        std::vector<ChunkCoord> changed;
        if (_dirty.empty()) return changed;

        std::unordered_set<ChunkCoord, ChunkCoordHash> toProcess;
        std::swap(toProcess, _dirty);

        for (const auto& cc : toProcess) {
            if (simulateChunk(cc))
                changed.push_back(cc);
        }
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

    static void worldToLocal(int wx, int wy, int wz, ChunkCoord& cc, int& lx, int& ly, int& lz) {
        int s = ChunkData::SIZE;
        cc = worldToChunk(wx, wy, wz);
        lx = wx - cc.x * s;
        ly = wy - cc.y * s;
        lz = wz - cc.z * s;
    }

    bool isSolidWorld(int wx, int wy, int wz) const {
        ChunkCoord cc; int lx, ly, lz;
        worldToLocal(wx, wy, wz, cc, lx, ly, lz);
        auto it = _terrain.find(cc);
        if (it == _terrain.end()) return true; // missing = solid (no flow into unknown)
        int P = ChunkData::PADDED;
        if (lx < 0 || lx >= P || ly < 0 || ly >= P || lz < 0 || lz >= P) return true;
        return it->second.values[lx][ly][lz] < 0.f;
    }

    uint8_t getWaterWorld(int wx, int wy, int wz) const {
        ChunkCoord cc; int lx, ly, lz;
        worldToLocal(wx, wy, wz, cc, lx, ly, lz);
        auto it = _water.find(cc);
        if (it == _water.end()) return 0;
        return it->second.get(lx, ly, lz);
    }

    void setWaterWorld(int wx, int wy, int wz, uint8_t level) {
        ChunkCoord cc; int lx, ly, lz;
        worldToLocal(wx, wy, wz, cc, lx, ly, lz);
        _water[cc].coord = cc;
        _water[cc].set(lx, ly, lz, level);
    }

    void markDirtyAround(int wx, int wy, int wz) {
        // Mark this chunk and any neighbor chunks that border this voxel
        _dirty.insert(worldToChunk(wx, wy, wz));
        _dirty.insert(worldToChunk(wx - 1, wy, wz));
        _dirty.insert(worldToChunk(wx + 1, wy, wz));
        _dirty.insert(worldToChunk(wx, wy - 1, wz));
        _dirty.insert(worldToChunk(wx, wy + 1, wz));
        _dirty.insert(worldToChunk(wx, wy, wz - 1));
        _dirty.insert(worldToChunk(wx, wy, wz + 1));
    }

    // Fill sea-level water when terrain chunk is first loaded
    void fillSeaLevel(ChunkCoord cc) {
        auto terrainIt = _terrain.find(cc);
        if (terrainIt == _terrain.end()) return;

        int S = ChunkData::SIZE;
        int ox = cc.x * S, oy = cc.y * S, oz = cc.z * S;

        WaterChunk& wc = _water[cc];
        wc.coord = cc;
        bool anyWater = false;

        for (int x = 0; x < S; x++)
        for (int z = 0; z < S; z++) {
            float fwx = (float)(ox + x);
            float fwz = (float)(oz + z);
            float waterY = getWaterSurfaceY(fwx, fwz);

            for (int y = 0; y < S; y++) {
                float wy = (float)(oy + y);
                bool solid = terrainIt->second.values[x][y][z] < 0.f;
                if (!solid && wy < waterY) {
                    wc.set(x, y, z, WATER_MAX);
                    anyWater = true;
                }
            }
        }

        if (!anyWater)
            _water.erase(cc);
    }

    // Simulate one chunk's water flow. Returns true if anything changed.
    bool simulateChunk(ChunkCoord cc) {
        auto waterIt = _water.find(cc);
        if (waterIt == _water.end()) return false;

        WaterChunk& wc = waterIt->second;
        int S = ChunkData::SIZE;
        int ox = cc.x * S, oy = cc.y * S, oz = cc.z * S;

        WaterChunk prev = wc; // snapshot for consistent reads
        bool changed = false;

        // Process bottom-to-top so gravity takes priority
        for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
        for (int z = 0; z < S; z++) {
            int wx = ox + x, wy = oy + y, wz = oz + z;

            // Clear water inside solid
            if (isSolidWorld(wx, wy, wz)) {
                if (prev.get(x, y, z) != 0) {
                    wc.set(x, y, z, 0);
                    changed = true;
                }
                continue;
            }

            uint8_t level = prev.get(x, y, z);

            if (level == 0) {
                // ── Empty cell: check if water should flow in ─────────

                // Gravity: water above falls down as source
                uint8_t above = getWaterWorld(wx, wy + 1, wz);
                if (above > 0 && !isSolidWorld(wx, wy + 1, wz)) {
                    wc.set(x, y, z, WATER_MAX);
                    changed = true;
                    markDirtyAround(wx, wy, wz);
                    continue;
                }

                // Horizontal: take (max neighbor level) - 1
                uint8_t best = 0;
                const int dx4[] = {-1, 1, 0, 0};
                const int dz4[] = {0, 0, -1, 1};
                for (int i = 0; i < 4; i++) {
                    uint8_t nl = getWaterWorld(wx + dx4[i], wy, wz + dz4[i]);
                    if (nl > best) best = nl;
                }
                if (best > 1) {
                    wc.set(x, y, z, (uint8_t)(best - 1));
                    changed = true;
                    markDirtyAround(wx, wy, wz);
                }
            } else {
                // ── Existing water: propagate flow ────────────────────

                // Gravity: push down
                if (!isSolidWorld(wx, wy - 1, wz)) {
                    uint8_t below = getWaterWorld(wx, wy - 1, wz);
                    if (below < WATER_MAX) {
                        markDirtyAround(wx, wy - 1, wz);
                    }
                }

                // Horizontal spread
                if (level >= 2) {
                    const int dx4[] = {-1, 1, 0, 0};
                    const int dz4[] = {0, 0, -1, 1};
                    for (int i = 0; i < 4; i++) {
                        int nx = wx + dx4[i], nz = wz + dz4[i];
                        if (!isSolidWorld(nx, wy, nz)) {
                            uint8_t nl = getWaterWorld(nx, wy, nz);
                            if (nl < level - 1) {
                                markDirtyAround(nx, wy, nz);
                            }
                        }
                    }
                }

                // Decay: non-source water that lost sustaining neighbor
                if (level < WATER_MAX && level > 0) {
                    bool sustained = false;

                    // Above sustains fully
                    if (getWaterWorld(wx, wy + 1, wz) > 0 && !isSolidWorld(wx, wy + 1, wz))
                        sustained = true;

                    // Any neighbor with higher level sustains
                    if (!sustained) {
                        const int dx4[] = {-1, 1, 0, 0};
                        const int dz4[] = {0, 0, -1, 1};
                        for (int i = 0; i < 4; i++) {
                            if (getWaterWorld(wx + dx4[i], wy, wz + dz4[i]) > level) {
                                sustained = true;
                                break;
                            }
                        }
                    }

                    if (!sustained) {
                        uint8_t newLevel = level > 1 ? (uint8_t)(level - 1) : (uint8_t)0;
                        if (newLevel != level) {
                            wc.set(x, y, z, newLevel);
                            changed = true;
                            markDirtyAround(wx, wy, wz);
                        }
                    }
                }
            }
        }

        if (wc.empty()) _water.erase(cc);
        return changed;
    }
};
