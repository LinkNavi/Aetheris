#include "noise_gen.h"

#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"

ChunkData generateChunk(ChunkCoord coord) {
    ChunkData data;
    data.coord = coord;

    constexpr int N = ChunkData::SIZE;
    constexpr float scale = 0.05f;

    for (int x = 0; x < N; x++)
    for (int y = 0; y < N; y++)
    for (int z = 0; z < N; z++) {
        float wx = (coord.x * N + x) * scale;
        float wy = (coord.y * N + y) * scale;
        float wz = (coord.z * N + z) * scale;

        // Layered octaves manually
        float noise = stb_perlin_fbm_noise3(wx, wy, wz, 2.0f, 0.5f, 4);

        // Vertical gradient so we get a ground surface
        float height = (coord.y * N + y) * 0.03f - 0.5f;
        data.values[x][y][z] = noise + height;
    }

    return data;
}
