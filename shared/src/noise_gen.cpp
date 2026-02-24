#include "noise_gen.h"
#include "config.h"
#include <cmath>
#include <cstdint>

// Seeded hash — same seed + same coords = same value always
static float hashNoise(int64_t seed, int x, int y, int z) {
    uint64_t h = (uint64_t)seed;
    h ^= (uint64_t)(x * 1619 + y * 31337 + z * 6971);
    h ^= h >> 16;
    h *= 0x45d9f3b37197344dULL;
    h ^= h >> 16;
    return (float)(h & 0xffffff) / (float)0xffffff; // 0..1
}

static float smoothstep(float t) { return t * t * (3.f - 2.f * t); }

static float valueNoise(int64_t seed, float x, float y, float z) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;
    float ux = smoothstep(fx), uy = smoothstep(fy), uz = smoothstep(fz);

    float v000 = hashNoise(seed, ix,   iy,   iz  );
    float v100 = hashNoise(seed, ix+1, iy,   iz  );
    float v010 = hashNoise(seed, ix,   iy+1, iz  );
    float v110 = hashNoise(seed, ix+1, iy+1, iz  );
    float v001 = hashNoise(seed, ix,   iy,   iz+1);
    float v101 = hashNoise(seed, ix+1, iy,   iz+1);
    float v011 = hashNoise(seed, ix,   iy+1, iz+1);
    float v111 = hashNoise(seed, ix+1, iy+1, iz+1);

    return v000 + ux*(v100-v000)
         + uy*(v010-v000 + ux*(v110-v010-v100+v000))
         + uz*(v001-v000 + ux*(v101-v001-v100+v000)
             + uy*(v011-v001-v010+v000 + ux*(v111-v011-v101+v001-v110+v010+v100-v000)));
}

static float fbm(int64_t seed, float x, float y, float z, int octaves) {
    float n = 0.f, amp = 0.5f, freq = 1.f;
    for (int o = 0; o < octaves; o++) {
        n   += valueNoise(seed + o * 1000, x*freq, y*freq, z*freq) * amp;
        amp  *= 0.5f;
        freq *= 2.f;
    }
    return n * 2.f - 1.f; // remap 0..1 to -1..1
}

ChunkData generateChunk(ChunkCoord coord) {
    ChunkData data;
    data.coord = coord;

    constexpr int     N      = ChunkData::SIZE;
    constexpr int     P      = ChunkData::PADDED;
    constexpr float scale  = 0.012f;  // bigger landmasses
constexpr float vscale = 0.02f;   // more vertical variation
    const     int64_t seed   = (int64_t)Config::WORLD_SEED;

    for (int x = 0; x < P; x++)
    for (int y = 0; y < P; y++)
    for (int z = 0; z < P; z++) {
        float wx = (coord.x * N + x) * scale;
        float wy = (coord.y * N + y) * vscale;
        float wz = (coord.z * N + z) * scale;

float n      = fbm(seed,       wx, wy, wz, 3);  // fewer octaves = chunkier
float detail = fbm(seed+99999, wx*2.f, wy*2.f, wz*2.f, 2) * 0.15f; // less detail
        float worldY   = (float)(coord.y * N + y);
        float gradient = 0.5f - worldY * 0.022f;

        data.values[x][y][z] = n + detail + gradient;
    }

    return data;
}
