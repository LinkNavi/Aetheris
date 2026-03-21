#pragma once
#include "chunk.h"
#include "tree_gen.h"

float             sampleSurfaceY(float wx, float wz);
ChunkData         generateChunk(ChunkCoord coord);

// Biome-aware water surface Y at a given world XZ.
// Returns the water level height. Terrain above this is dry land.
// Can vary per-biome for swamps, mountain lakes, canyons, etc.
float             getWaterSurfaceY(float wx, float wz);

// Call once at startup before any chunk generation
void              initTreeLibrary(int64_t seed);

// Access the global tree library (for tree system registration)
const TreeLibrary& getTreeLibrary();
