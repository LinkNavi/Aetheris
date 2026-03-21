#pragma once
#include "chunk.h"
#include "tree_gen.h"

float             sampleSurfaceY(float wx, float wz);
ChunkData         generateChunk(ChunkCoord coord);

// Call once at startup before any chunk generation
void              initTreeLibrary(int64_t seed);

// Access the global tree library (for tree system registration)
const TreeLibrary& getTreeLibrary();
