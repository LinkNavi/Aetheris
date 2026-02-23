#pragma once
#include "chunk.h"

// Fills a ChunkData scalar field using FastNoise2.
// Values < 0 are inside the surface.
ChunkData generateChunk(ChunkCoord coord);
