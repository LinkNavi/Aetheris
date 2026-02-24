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
float sampleSurfaceY(float wx, float wz) {
    constexpr float hscale   = 0.008f;
    constexpr float hHeight  = 80.f;
    constexpr float seaLevel = 64.f;
    const int64_t   seed     = (int64_t)Config::WORLD_SEED;

    float base   = fbm(seed,         wx * hscale,        0.f, wz * hscale,        4);
    float detail = fbm(seed + 111111, wx * hscale * 3.f, 0.f, wz * hscale * 3.f, 3) * 0.25f;
    return seaLevel + (base + detail) * hHeight + 2.f;
}
ChunkData generateChunk(ChunkCoord coord) {
    ChunkData data;
    data.coord = coord;

    constexpr int     N        = ChunkData::SIZE;
    constexpr int     P        = ChunkData::PADDED;
    const     int64_t seed     = (int64_t)Config::WORLD_SEED;
    constexpr float   hscale   = 0.008f;
    constexpr float   hHeight  = 80.f;
    constexpr float   seaLevel = 64.f;

    for (int x = 0; x < P; x++)
    for (int z = 0; z < P; z++) {
        float wx = (float)(coord.x * N + x);
        float wz = (float)(coord.z * N + z);

        float base    = fbm(seed,         wx * hscale,        0.f, wz * hscale,        4);
        float detail  = fbm(seed + 111111, wx * hscale * 3.f, 0.f, wz * hscale * 3.f, 3) * 0.25f;
        float surfaceY = seaLevel + (base + detail) * hHeight;

        for (int y = 0; y < P; y++) {
            float wy = (float)(coord.y * N + y);

            float cave = 0.f;
            if (wy < surfaceY - 4.f) {
                float c1 = fbm(seed + 222222, wx * 0.018f,        wy * 0.018f,        wz * 0.018f,        2);
                float c2 = fbm(seed + 333333, wx * 0.018f + 5.f,  wy * 0.018f + 5.f,  wz * 0.018f + 5.f,  2);
                cave = (1.f - std::abs(c1 * c2) * 4.f) * 0.6f;
            }

            float density = surfaceY - wy + cave;
            if (density >  2.f) density =  2.f;
            if (density < -2.f) density = -2.f;
            data.values[x][y][z] = -density;
        }
    }

    return data;
}
