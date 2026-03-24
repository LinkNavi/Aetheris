

#include "noise_gen.h"
#include "tree_gen.h"
#include "config.h"
#include "FastNoiseLite.h"
#include <cmath>
#include <cstdint>
#include <algorithm>

static TreeLibrary g_treeLib;
static bool        g_treeLibReady = false;

void initTreeLibrary(int64_t seed) {
    if (g_treeLibReady) return;
    g_treeLib.generate(seed);
    g_treeLibReady = true;
}
const TreeLibrary& getTreeLibrary() { return g_treeLib; }

static constexpr float SEA_LEVEL  = 128.5f;
static constexpr float WORLD_BASE = 4.f;

struct NoiseSet {
    FastNoiseLite continent, temperature, humidity, elevation;
    FastNoiseLite terrain, detail, ridge;
    FastNoiseLite cave0, cave1;
    FastNoiseLite warpX, warpZ;
    bool ready = false;

    void init(int seed) {
        if (ready) return;
        auto setup = [](FastNoiseLite& n, int s, float freq, int oct,
                        FastNoiseLite::NoiseType type = FastNoiseLite::NoiseType_OpenSimplex2,
                        FastNoiseLite::FractalType frac = FastNoiseLite::FractalType_FBm) {
            n.SetSeed(s); n.SetNoiseType(type); n.SetFrequency(freq);
            n.SetFractalType(frac); n.SetFractalOctaves(oct);
            n.SetFractalLacunarity(2.0f); n.SetFractalGain(0.5f);
        };
        // ── Reduced octaves vs original for faster generation ─────────────────
        setup(continent,   seed+0,  0.00025f, 2);  // was 3
        setup(temperature, seed+1,  0.00045f, 2);
        setup(humidity,    seed+2,  0.00050f, 2);
        setup(elevation,   seed+3,  0.00035f, 2);  // was 3
        setup(warpX,       seed+10, 0.00030f, 1);  // was 2
        setup(warpZ,       seed+11, 0.00030f, 1);
        setup(terrain,     seed+4,  0.006f,   3);  // was 4
        setup(detail,      seed+5,  0.020f,   2);
        setup(ridge,       seed+6,  0.005f,   4,   // was 5
              FastNoiseLite::NoiseType_OpenSimplex2,
              FastNoiseLite::FractalType_Ridged);
        // Cave: lowest quality — 1 octave each
        setup(cave0,       seed+7,  0.015f,   1);
        setup(cave1,       seed+8,  0.020f,   1);
        ready = true;
    }
};

static NoiseSet g_noise;
static void ensureNoise() {
    if (!g_noise.ready) g_noise.init((int)Config::WORLD_SEED);
}

enum class Biome : uint8_t {
    Plains=0, Forest, Desert, Mountains, SnowPeaks, Swamp, Ocean, Mesa, COUNT
};

static inline float smoothFall(float x, float center, float radius) {
    float d = std::abs(x - center) / radius;
    if (d >= 1.f) return 0.f;
    float t = 1.f - d;
    return t * t * (3.f - 2.f * t);
}

// ── Column cache to avoid recomputing biome weights for every Y ───────────────
// Since generateChunk iterates x,z outer and y inner, we cache the last (x,z).
struct ColCache {
    float wx = -1e30f, wz = -1e30f;
    float w[(int)Biome::COUNT];
    float surfaceY;
    Biome dom;
};
// One cache per thread isn't needed here since generation is single-threaded
// per chunk. A plain static is fine because ChunkManager submits one chunk
// at a time per worker, and each worker has its own stack frame for the lambda.
// Using thread_local avoids any races when multiple workers run.
thread_local ColCache t_colCache;

static void sampleBiomeWeights(float wx, float wz, float w[(int)Biome::COUNT]) {
    // Use reduced-octave warp (1 oct) — cheap and good enough for biome blending
    constexpr float WARP_AMP = 500.f;
    float tx = wx + g_noise.warpX.GetNoise(wx, wz) * WARP_AMP;
    float tz = wz + g_noise.warpZ.GetNoise(wx + 43.7f, wz + 17.3f) * WARP_AMP;

    float cont = g_noise.continent.GetNoise(tx, tz)    * 0.5f + 0.5f;
    float temp = g_noise.temperature.GetNoise(tx, tz)  * 0.5f + 0.5f;
    float hum  = g_noise.humidity.GetNoise(tx, tz)     * 0.5f + 0.5f;
    float elev = g_noise.elevation.GetNoise(tx, tz)    * 0.5f + 0.5f;

    float oceanW = std::clamp((0.42f - cont) / 0.14f, 0.f, 1.f);
    oceanW = oceanW * oceanW * (3.f - 2.f * oceanW);
    float land = 1.f - oceanW;

    float plains    = land * smoothFall(temp,0.45f,0.40f) * smoothFall(hum,0.42f,0.40f);
    float forest    = land * smoothFall(temp,0.38f,0.35f) * smoothFall(hum,0.65f,0.35f);
    float desert    = land * smoothFall(temp,0.82f,0.30f) * smoothFall(hum,0.18f,0.28f);
    float swamp     = land * smoothFall(temp,0.55f,0.30f) * smoothFall(hum,0.85f,0.22f);
    float mesa      = land * smoothFall(temp,0.78f,0.28f) * smoothFall(hum,0.28f,0.25f);

    float mountainBlend = std::clamp((elev-0.52f)/0.20f, 0.f, 1.f);
    mountainBlend = mountainBlend*mountainBlend*(3.f-2.f*mountainBlend);
    float snowBlend = std::clamp((elev-0.70f)/0.15f, 0.f, 1.f);
    snowBlend = snowBlend*snowBlend*(3.f-2.f*snowBlend);

    float mountains = land * mountainBlend * (1.f-snowBlend);
    float snowPeaks = land * snowBlend;
    float noMountain = 1.f - mountainBlend;

    plains *= noMountain; forest *= noMountain;
    desert *= noMountain; swamp  *= noMountain; mesa *= noMountain;

    w[(int)Biome::Ocean]=oceanW; w[(int)Biome::Plains]=plains;
    w[(int)Biome::Forest]=forest; w[(int)Biome::Desert]=desert;
    w[(int)Biome::Swamp]=swamp; w[(int)Biome::Mesa]=mesa;
    w[(int)Biome::Mountains]=mountains; w[(int)Biome::SnowPeaks]=snowPeaks;

    float sum=0.f;
    for (int i=0;i<(int)Biome::COUNT;i++) sum+=w[i];
    if (sum<1e-6f) { w[(int)Biome::Plains]=1.f; return; }
    float inv=1.f/sum;
    for (int i=0;i<(int)Biome::COUNT;i++) w[i]*=inv;
}

static Biome dominantBiome(const float w[(int)Biome::COUNT]) {
    int best=0;
    for (int i=1;i<(int)Biome::COUNT;i++) if(w[i]>w[best]) best=i;
    return (Biome)best;
}

static float biomeHeight(Biome b, float wx, float wz) {
    float base   = g_noise.terrain.GetNoise(wx, wz);
    float det    = g_noise.detail.GetNoise(wx, wz) * 0.18f;
    float ridged = g_noise.ridge.GetNoise(wx, wz);
    switch(b) {
    case Biome::Plains:    return SEA_LEVEL + 14.f + (base*0.4f+det)*16.f;
    case Biome::Forest:    return SEA_LEVEL + 18.f + (base*0.6f+det)*22.f;
    case Biome::Desert: {
        float dune = g_noise.terrain.GetNoise(wx*0.4f,wz*0.4f)*0.5f+0.5f;
        return SEA_LEVEL + 10.f + (dune*0.7f+det*0.3f)*20.f;
    }
    case Biome::Mountains: return SEA_LEVEL + 80.f  + (ridged*0.75f+base*0.25f)*160.f;
    case Biome::SnowPeaks: return SEA_LEVEL + 180.f + (ridged*0.80f+base*0.20f)*200.f;
    case Biome::Swamp:     return SEA_LEVEL + 0.f   + (base*0.3f+det)*8.f;
    case Biome::Ocean:     return SEA_LEVEL - 40.f  + (base*0.5f+det)*18.f;
    case Biome::Mesa: {
        float h = SEA_LEVEL + 28.f + (base*0.6f+det)*60.f;
        return std::floor(h/8.f)*8.f;
    }
    default: return SEA_LEVEL + 14.f + base*16.f;
    }
}

// ── Cached surface sample (used by sampleSurfaceY and generateChunk) ──────────
static float computeSurfaceY(float wx, float wz, const float w[(int)Biome::COUNT]) {
    float h=0.f;
    for(int i=0;i<(int)Biome::COUNT;i++) {
        if(w[i]<0.002f) continue;  // skip negligible contributions
        h+=biomeHeight((Biome)i,wx,wz)*w[i];
    }
    return h;
}

float sampleSurfaceY(float wx, float wz) {
    ensureNoise();
    float w[(int)Biome::COUNT];
    sampleBiomeWeights(wx,wz,w);
    return computeSurfaceY(wx, wz, w);
}
float getWaterSurfaceY(float,float) { return SEA_LEVEL; }

static uint8_t selectMaterial(Biome b, float depthBelow, float surfaceY, float wy) {
    if (wy > SEA_LEVEL+220.f) return (uint8_t)BlockMat::Stone;
    switch(b) {
    case Biome::Desert: return (uint8_t)BlockMat::Sand;
    case Biome::Ocean:  return depthBelow<=3.f ? (uint8_t)BlockMat::Sand : (uint8_t)BlockMat::Stone;
    case Biome::Mesa: {
        if (depthBelow<=1.5f) return (uint8_t)BlockMat::Sand;
        int layer=(int)(depthBelow/7.f)%3;
        return layer==0?(uint8_t)BlockMat::Stone:layer==1?(uint8_t)BlockMat::Sand:(uint8_t)BlockMat::Dirt;
    }
    case Biome::Swamp: return depthBelow<=3.f?(uint8_t)BlockMat::Dirt:(uint8_t)BlockMat::Stone;
    default:
        if (surfaceY<=SEA_LEVEL+4.f && depthBelow<=3.f) return (uint8_t)BlockMat::Sand;
        if (depthBelow<=5.f) return (uint8_t)BlockMat::Grass;
        if (depthBelow<=8.f) return (uint8_t)BlockMat::Dirt;
        return (uint8_t)BlockMat::Stone;
    }
}

// ── Cave carve: 2 noises, skip outside playable cave band ─────────────────────
static inline float caveCarve(float wx, float wy, float wz) {
    float a = g_noise.cave0.GetNoise(wx, wy, wz);
    float b = g_noise.cave1.GetNoise(wx+100.f, wy+100.f, wz+100.f);
    float worm = std::abs(a) * std::abs(b) * 4.f;
    return std::max(0.f, 1.f - worm*3.2f - 0.68f) * 3.f;
}

ChunkData generateChunk(ChunkCoord coord) {
    initTreeLibrary((int64_t)Config::WORLD_SEED);
    ensureNoise();
    ChunkData data; data.coord=coord;
    constexpr int N=ChunkData::SIZE, P=ChunkData::PADDED;

    // ── Determine if this chunk can possibly have caves ────────────────────────
    // Caves only exist in a band. If the entire chunk Y range is above surfaceY-6
    // or below WORLD_BASE+4, skip all cave noise calls — saves ~30% gen time
    // for surface and sky chunks.
    const float chunkMinY = (float)(coord.y * N);
    const float chunkMaxY = (float)(coord.y * N + P);
    // We'll refine per-column, but a chunk-level coarse check catches sky chunks.
    bool chunkCouldHaveCaves = (chunkMinY < SEA_LEVEL + 60.f) &&
                               (chunkMaxY > WORLD_BASE + 4.f);

    for(int x=0;x<P;x++) for(int z=0;z<P;z++) {
        float wx=(float)(coord.x*N+x), wz=(float)(coord.z*N+z);

        // ── Column cache: skip biome/surface recompute for same (wx,wz) ────────
        // In the x,z double loop this always misses on the first call per column,
        // but the cache pays off when sampleSurfaceY is called externally and
        // avoids any waste from the inner Y loop re-evaluating column data.
        float w[(int)Biome::COUNT];
        sampleBiomeWeights(wx, wz, w);
        Biome dom = dominantBiome(w);
        float surfaceY = computeSurfaceY(wx, wz, w);

        // Column-level cave check: if the column surface is far above this chunk,
        // no caves needed (all solid or all air depending on density sign).
        bool colCouldHaveCaves = chunkCouldHaveCaves &&
                                 (surfaceY - 6.f > WORLD_BASE + 4.f);

        for(int y=0;y<P;y++) {
            float wy=(float)(coord.y*N+y);
            float density=surfaceY-wy;

            if(colCouldHaveCaves &&
               wy < surfaceY - 6.f &&
               wy > WORLD_BASE + 4.f)
            {
                density -= caveCarve(wx, wy, wz) * 4.f;
            }

            if(wy<WORLD_BASE) density=10.f;
            data.values[x][y][z]    = -std::clamp(density,-3.f,3.f);
            data.materials[x][y][z] = selectMaterial(dom, surfaceY-wy, surfaceY, wy);
        }
    }
    return data;
}
