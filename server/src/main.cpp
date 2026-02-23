#include "noise_gen.h"
#include "marching_cubes.h"
#include <iostream>

int main() {
    // Generate a 3x3x3 grid of chunks around origin for now
    for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
    for (int z = -1; z <= 1; z++) {
        ChunkCoord coord{x, y, z};
        ChunkData  data = generateChunk(coord);
        ChunkMesh  mesh = marchChunk(data);
        std::cout << "Chunk (" << x << "," << y << "," << z << ") — "
                  << mesh.vertices.size() << " verts, "
                  << mesh.indices.size()  << " indices\n";
    }
}
