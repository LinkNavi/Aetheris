#pragma once
#include "chunk.h"

// Takes a filled ChunkData scalar field and returns a mesh.
// Values < 0 are considered inside the surface.
ChunkMesh marchChunk(const ChunkData& chunk);
