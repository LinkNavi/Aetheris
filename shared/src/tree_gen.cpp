#include "tree_gen.h"
#include <algorithm>
#include <cstring>

// ── Hash noise ────────────────────────────────────────────────────────────────

float TreeLibrary::hashf(int64_t seed, int x, int z) {
    uint64_t h = (uint64_t)seed;
    h ^= (uint64_t)(x * 1619 + z * 31337);
    h ^= h >> 16;
    h *= 0x45d9f3b37197344dULL;
    h ^= h >> 16;
    return (float)(h & 0xffffff) / (float)0xffffff;
}

float TreeLibrary::hashf3(int64_t seed, float x, float y, float z) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
    uint64_t h = (uint64_t)seed;
    h ^= (uint64_t)(ix * 1619 + iy * 31337 + iz * 6971);
    h ^= h >> 16;
    h *= 0x45d9f3b37197344dULL;
    h ^= h >> 16;
    return (float)(h & 0xffffff) / (float)0xffffff;
}

// ── Template generation ───────────────────────────────────────────────────────

void TreeLibrary::buildTrunk(TreeTemplate& t, int height, float trunkRadius,
                              int64_t seed) const {
    for (int dy = 0; dy < height; dy++) {
        // Slight taper — trunk gets narrower near top
        float r = trunkRadius * (1.f - (float)dy / (float)(height * 2));
        int ri = (int)std::ceil(r) + 1;

        for (int dx = -ri; dx <= ri; dx++)
        for (int dz = -ri; dz <= ri; dz++) {
            float dist = std::sqrt((float)(dx*dx + dz*dz));
            // Noise-perturbed radius for organic look
            float noisePerturb = hashf3(seed, (float)dx, (float)dy, (float)dz) * 0.4f;
            if (dist > r + noisePerturb) continue;

            float density = -(r + noisePerturb - dist + 0.3f); // more negative = more solid
            density = std::max(density, -2.f);

            TreeVoxel v;
            v.dx = dx; v.dy = dy; v.dz = dz;
            v.density  = density;
            v.material = (uint8_t)BlockMat::Dirt; // reusing Dirt as bark for now
            v.noiseOff = hashf3(seed + 1, (float)dx*0.5f, (float)dy, (float)dz*0.5f);
            t.voxels.push_back(v);
        }
    }
}

void TreeLibrary::buildCanopy(TreeTemplate& t, float radius, int trunkHeight,
                               float yOff, int64_t seed) const {
    int ri = (int)std::ceil(radius) + 1;
    float cy = (float)trunkHeight + yOff;

    for (int dx = -ri; dx <= ri; dx++)
    for (int dy = -ri; dy <= ri + 2; dy++)  // slightly taller than wide
    for (int dz = -ri; dz <= ri; dz++) {
        float wx = (float)dx;
        float wy = (float)dy - cy + (float)trunkHeight;
        float wz = (float)dz;

        // Ellipsoid — slightly flattened on bottom, extended on top
        float ex = wx / (radius * 1.0f);
        float ey = wy / (radius * 1.3f);
        float ez = wz / (radius * 1.0f);
        float dist = std::sqrt(ex*ex + ey*ey + ez*ez);

        // Noise perturbation for organic clumping
        float n = hashf3(seed + 99, wx * 0.4f, wy * 0.4f + 100.f, wz * 0.4f);
        float perturb = n * 0.35f;

        if (dist > 1.f + perturb) continue;

        // Don't overwrite trunk voxels (trunk is at dx=0±trunkRadius, check roughly)
        float trunkDist = std::sqrt((float)(dx*dx + dz*dz));
        if (trunkDist < 0.8f && dy < (int)(trunkHeight - (float)trunkHeight * 0.3f))
            continue;

        float density = -(1.f + perturb - dist + 0.2f);
        density = std::max(density, -1.5f);

        TreeVoxel v;
        v.dx = dx;
        v.dy = (int)trunkHeight + dy - (int)cy + (int)trunkHeight;
        v.dz = dz;
        v.density  = density;
        v.material = (uint8_t)BlockMat::Grass; // reusing Grass as leaves for now
        v.noiseOff = hashf3(seed + 200, wx * 0.3f, wy * 0.3f, wz * 0.3f);
        t.voxels.push_back(v);
    }
}

void TreeLibrary::generate(int64_t seed) {
    _seed = seed;

    // Generate COUNT variations with different sizes/shapes
    for (int i = 0; i < COUNT; i++) {
        TreeTemplate t;
        float fi = (float)i / (float)COUNT;

        // Vary trunk height 4-9 voxels
        t.trunkHeight  = 4 + (int)(fi * 5.f) + (int)(hashf(seed + i, i, 0) * 2.f);
        // Vary canopy radius 2.5-5.0
        t.canopyRadius = 2.5f + fi * 2.5f + hashf(seed + i, i, 1) * 0.5f;
        // Canopy Y offset — sits right at trunk top or slightly overlapping
        t.canopyYOff   = -1.5f - hashf(seed + i, i, 2) * 1.0f;

        float trunkRadius = 0.55f + hashf(seed + i, i, 3) * 0.3f;

        buildTrunk (t, t.trunkHeight, trunkRadius,    seed * 7 + i * 113);
        buildCanopy(t, t.canopyRadius, t.trunkHeight, t.canopyYOff, seed * 13 + i * 97);

        _templates[i] = std::move(t);
    }
}

// ── Placement sampling ────────────────────────────────────────────────────────

int TreeLibrary::samplePlacement(float wx, float wy, float wz,
                                  float surfaceY) const {
    // Only place trees near the surface
    if (std::abs(wy - surfaceY) > 2.f) return -1;

    // Jitter the sample position so trees don't align to a grid
    int gx = (int)std::floor(wx / 8.f);
    int gz = (int)std::floor(wz / 8.f);

    float jx = hashf(_seed + 7, gx, gz) * 7.f;
    float jz = hashf(_seed + 8, gx + 100, gz + 100) * 7.f;

    float cellX = (float)gx * 8.f + jx;
    float cellZ = (float)gz * 8.f + jz;

    // Only the closest point in the cell can spawn a tree
    if (std::abs(wx - cellX) > 1.5f || std::abs(wz - cellZ) > 1.5f)
        return -1;

    // Density — some cells don't have trees at all
    float density = hashf(_seed + 9, gx * 3, gz * 5);
    if (density < 0.35f) return -1;  // ~35% of cells have trees

    // Pick template
    float templateF = hashf(_seed + 11, gx + 999, gz + 999);
    return (int)(templateF * (float)COUNT) % COUNT;
}

// ── Stamp into chunk data ─────────────────────────────────────────────────────

void TreeLibrary::stamp(ChunkData& data, const TreeTemplate& tmpl,
                         float wx, float wy, float wz) const {
    int N = ChunkData::SIZE;
    int P = ChunkData::PADDED;

    int baseX = (int)std::floor(wx);
    int baseY = (int)std::floor(wy);
    int baseZ = (int)std::floor(wz);

    int chunkOriginX = data.coord.x * N;
    int chunkOriginY = data.coord.y * N;
    int chunkOriginZ = data.coord.z * N;

    for (const auto& v : tmpl.voxels) {
        int wx2 = baseX + v.dx;
        int wy2 = baseY + v.dy;
        int wz2 = baseZ + v.dz;

        // Convert to local chunk coordinates
        int lx = wx2 - chunkOriginX;
        int ly = wy2 - chunkOriginY;
        int lz = wz2 - chunkOriginZ;

        if (lx < 0 || lx >= P || ly < 0 || ly >= P || lz < 0 || lz >= P)
            continue;

        // Only overwrite if this voxel would make it more solid
        // (don't carve through terrain)
        if (v.density < data.values[lx][ly][lz])
            data.values[lx][ly][lz] = v.density;

        data.materials[lx][ly][lz] = v.material;
    }
}
