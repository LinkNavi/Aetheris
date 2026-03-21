#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cmath>
#include "water_sim.h"
#include "chunk.h"

// ── WaterSimulator ────────────────────────────────────────────────────────────
// Server-side cellular automaton water simulation.
// Runs at ~4Hz. When blocks change or water sources are added,
// call markDirty() to schedule re-simulation for that chunk.
//
// Terrain density is read-only — water flows around solid voxels.
// Water does NOT modify the marching cubes density field.

class WaterSimulator {
public:
    static constexpr float TICK_RATE = 0.25f; // seconds between sim ticks

    // Register terrain chunk (needed to know where water can flow)
    void addTerrain(const ChunkData& chunk) {
        _terrain[chunk.coord] = chunk;
    }

    void removeTerrain(ChunkCoord coord) {
        _terrain.erase(coord);
        _water.erase(coord);
        _dirty.erase(coord);
    }

    // Place a water source block at world position
    void placeWater(int wx, int wy, int wz, uint8_t level = WATER_MAX) {
        ChunkCoord cc = worldToChunk(wx, wy, wz);
        auto& wc = _water[cc];
        wc.coord = cc;
        int lx = wx - cc.x * ChunkData::SIZE;
        int ly = wy - cc.y * ChunkData::SIZE;
        int lz = wz - cc.z * ChunkData::SIZE;
        if (lx>=0&&lx<ChunkData::SIZE&&ly>=0&&ly<ChunkData::SIZE&&lz>=0&&lz<ChunkData::SIZE) {
            wc.set(lx, ly, lz, level);
            _dirty.insert(cc);
            // Mark neighbours dirty too
            for (auto& n : neighbours(cc)) _dirty.insert(n);
        }
    }

    // Remove water at world position (e.g. player placed a block there)
    void removeWater(int wx, int wy, int wz) {
        placeWater(wx, wy, wz, 0);
    }

    // Called when a terrain block is removed — water may flow in
    void onBlockRemoved(int wx, int wy, int wz) {
        ChunkCoord cc = worldToChunk(wx, wy, wz);
        _dirty.insert(cc);
        for (auto& n : neighbours(cc)) _dirty.insert(n);
    }

    // Called when a terrain block is placed — may displace water
    void onBlockPlaced(int wx, int wy, int wz) {
        ChunkCoord cc = worldToChunk(wx, wy, wz);
        // Remove water at that position
        auto it = _water.find(cc);
        if (it != _water.end()) {
            int lx = wx - cc.x * ChunkData::SIZE;
            int ly = wy - cc.y * ChunkData::SIZE;
            int lz = wz - cc.z * ChunkData::SIZE;
            it->second.set(lx, ly, lz, 0);
        }
        _dirty.insert(cc);
    }

    // Per-tick update. Returns set of chunk coords whose water changed.
    // Call at TICK_RATE frequency from server tick.
    std::vector<ChunkCoord> tick() {
        std::vector<ChunkCoord> changed;
        if (_dirty.empty()) return changed;

        // Process dirty chunks
        std::unordered_set<ChunkCoord, ChunkCoordHash> nextDirty;

        for (const auto& cc : _dirty) {
            bool anyChange = simulateChunk(cc, nextDirty);
            if (anyChange) changed.push_back(cc);
        }

        _dirty = nextDirty;
        return changed;
    }

    // Get water chunk for mesh building / serialization
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
    std::unordered_map<ChunkCoord, ChunkData,   ChunkCoordHash> _terrain;
    std::unordered_map<ChunkCoord, WaterChunk,  ChunkCoordHash> _water;
    std::unordered_set<ChunkCoord, ChunkCoordHash>              _dirty;

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

    // Returns true if terrain voxel at world pos is solid
    bool isSolid(int wx, int wy, int wz) const {
        ChunkCoord cc = worldToChunk(wx, wy, wz);
        auto it = _terrain.find(cc);
        if (it == _terrain.end()) return true; // unknown = treat as solid
        int lx = wx - cc.x * ChunkData::SIZE;
        int ly = wy - cc.y * ChunkData::SIZE;
        int lz = wz - cc.z * ChunkData::SIZE;
        int P = ChunkData::PADDED;
        if (lx<0||lx>=P||ly<0||ly>=P||lz<0||lz>=P) return true;
        return it->second.values[lx][ly][lz] < 0.f; // negative = solid
    }

    uint8_t getWaterLevel(int wx, int wy, int wz) const {
        ChunkCoord cc = worldToChunk(wx, wy, wz);
        auto it = _water.find(cc);
        if (it == _water.end()) return 0;
        int lx = wx - cc.x * ChunkData::SIZE;
        int ly = wy - cc.y * ChunkData::SIZE;
        int lz = wz - cc.z * ChunkData::SIZE;
        int S = ChunkData::SIZE;
        if (lx<0||lx>=S||ly<0||ly>=S||lz<0||lz>=S) return 0;
        return it->second.get(lx, ly, lz);
    }

    void setWaterLevel(int wx, int wy, int wz, uint8_t level) {
        ChunkCoord cc = worldToChunk(wx, wy, wz);
        auto& wc = _water[cc];
        wc.coord = cc;
        int lx = wx - cc.x * ChunkData::SIZE;
        int ly = wy - cc.y * ChunkData::SIZE;
        int lz = wz - cc.z * ChunkData::SIZE;
        int S = ChunkData::SIZE;
        if (lx<0||lx>=S||ly<0||ly>=S||lz<0||lz>=S) return;
        wc.set(lx, ly, lz, level);
    }

    bool simulateChunk(ChunkCoord cc,
                       std::unordered_set<ChunkCoord, ChunkCoordHash>& nextDirty) {
        int S = ChunkData::SIZE;
        int ox = cc.x * S, oy = cc.y * S, oz = cc.z * S;
        bool anyChange = false;

        // Process top-to-bottom for gravity, then horizontal spread
        for (int y = S - 1; y >= 0; y--)
        for (int x = 0; x < S; x++)
        for (int z = 0; z < S; z++) {
            int wx = ox + x, wy = oy + y, wz = oz + z;
            uint8_t level = getWaterLevel(wx, wy, wz);
            if (level == 0) continue;

            // ── Gravity: fall down ────────────────────────────────────────
            if (!isSolid(wx, wy-1, wz)) {
                uint8_t below = getWaterLevel(wx, wy-1, wz);
                if (below < WATER_MAX) {
                    uint8_t give = std::min((uint8_t)(WATER_MAX - below), level);
                    setWaterLevel(wx, wy-1, wz, below + give);
                    level -= give;
                    setWaterLevel(wx, wy, wz, level);
                    anyChange = true;

                    ChunkCoord belowChunk = worldToChunk(wx, wy-1, wz);
                    nextDirty.insert(belowChunk);
                    nextDirty.insert(cc);
                }
            }

            if (level == 0) continue;

            // ── Horizontal spread ─────────────────────────────────────────
            // Only spread if above solid or on full water column
            bool onSolid = isSolid(wx, wy-1, wz) ||
                           getWaterLevel(wx, wy-1, wz) == WATER_MAX;
            if (!onSolid) continue;

            // Find lower or empty horizontal neighbours
            struct Neighbour { int dx, dz; };
            static const Neighbour dirs[] = {{1,0},{-1,0},{0,1},{0,-1}};

            // Count how many neighbours can receive
            int spreadCount = 0;
            for (auto& d : dirs) {
                int nx = wx+d.dx, nz = wz+d.dz;
                if (!isSolid(nx, wy, nz) && getWaterLevel(nx, wy, nz) < level - 1)
                    spreadCount++;
            }

            if (spreadCount == 0) continue;

            // Distribute evenly
            uint8_t give = 1;
            for (auto& d : dirs) {
                int nx = wx+d.dx, nz = wz+d.dz;
                if (!isSolid(nx, wy, nz)) {
                    uint8_t nlevel = getWaterLevel(nx, wy, nz);
                    if (nlevel < level - 1 && level > 1) {
                        setWaterLevel(nx, wy, nz, nlevel + give);
                        level--;
                        setWaterLevel(wx, wy, wz, level);
                        anyChange = true;

                        ChunkCoord nc = worldToChunk(nx, wy, nz);
                        nextDirty.insert(nc);
                        nextDirty.insert(cc);

                        if (level <= 1) break;
                    }
                }
            }
        }
        return anyChange;
    }
};
