// shared/src/marching_cubes.cpp
// Low-end optimization: replace the unordered_map<Vec3Key, glm::vec3> normal
// accumulator with a flat array indexed by quantized voxel position.
//
// The original uses a hash map keyed on quantized vertex positions to accumulate
// area-weighted normals. For a 32^3 chunk with ~5-8 verts per active voxel,
// that's thousands of hash map insertions/lookups per chunk. On a Sandy Bridge
// i5 with a cold L2 cache this dominates chunk meshing time.
//
// Replacement: since vertex positions are always within [0, PADDED) in each
// dimension after quantization at 1024 units per voxel unit, we can use a
// 3D flat array of size PADDED*1024 — but that's too large. Instead we
// quantize to voxel-grid resolution (round to nearest 0.5) and use a
// (2*PADDED)^3 array. Each entry is 12 bytes (glm::vec3). 
// 2*33 = 66 per axis → 66^3 = 287,496 entries × 12 bytes = ~3.3 MB on the
// stack is too much. Use a flat vector allocated once and reused.
//
// Actually the simplest win: use a fixed resolution quantization that maps
// into a (PADDED*2)^3 grid. PADDED=33, so 66^3 = 287K entries × 12 = 3.4MB.
// Allocate it as a static thread_local vector to avoid per-call allocation.

#include "marching_cubes.h"
#include "table.h"
#include <array>
#include <vector>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <cstring>

static glm::vec3 interp(float iso, glm::vec3 p0, float v0, glm::vec3 p1, float v1) {
    return (std::abs(v0-iso) < std::abs(v1-iso)) ? p0 : p1;
}
static uint8_t pickMat(float iso, const float vals[8], const uint8_t mats[8], int a, int b) {
    return (vals[a] < vals[b]) ? mats[a] : mats[b];
}

static inline uint32_t packNeutralTint(uint8_t mat) {
    return (uint32_t)mat | (128u<<8) | (128u<<16) | (128u<<24);
}

static uint8_t triMaterial(const uint8_t edgeMats[12], int e0, int e1, int e2,
                            const float vals[8]) {
    uint8_t m0 = edgeMats[e0], m1 = edgeMats[e1], m2 = edgeMats[e2];
    if (m0 == m1 || m0 == m2) return m0;
    if (m1 == m2)              return m1;
    auto edgeDensity = [&](int e) {
        int a = edgePairs[e][0], b = edgePairs[e][1];
        return std::min(std::abs(vals[a]), std::abs(vals[b]));
    };
    float d0 = edgeDensity(e0), d1 = edgeDensity(e1), d2 = edgeDensity(e2);
    if (d0 <= d1 && d0 <= d2) return m0;
    if (d1 <= d2)              return m1;
    return m2;
}

// ── Flat normal accumulator ───────────────────────────────────────────────────
// Vertices produced by marching cubes lie on voxel edges, so their positions
// (in local chunk space) are multiples of 0.5 in the range [0, PADDED).
// Quantize to half-voxel grid: index = round(pos * 2).
// Grid size: PADDED * 2 per axis = 66. Total = 66^3 = 287,496.
static constexpr int NORM_GRID = ChunkData::PADDED * 2;  // 66
static constexpr int NORM_SIZE = NORM_GRID * NORM_GRID * NORM_GRID;

thread_local std::vector<glm::vec3> t_normAccum;
thread_local std::vector<uint8_t>   t_normUsed;  // 1 bit per entry (bool)

static inline int normIdx(glm::vec3 p) {
    int ix = (int)std::round(p.x * 2.f);
    int iy = (int)std::round(p.y * 2.f);
    int iz = (int)std::round(p.z * 2.f);
    // Clamp to grid bounds
    ix = std::max(0, std::min(NORM_GRID-1, ix));
    iy = std::max(0, std::min(NORM_GRID-1, iy));
    iz = std::max(0, std::min(NORM_GRID-1, iz));
    return ix * NORM_GRID * NORM_GRID + iy * NORM_GRID + iz;
}

ChunkMesh marchChunk(const ChunkData& chunk) {
    ChunkMesh mesh; mesh.coord = chunk.coord;
    constexpr int   N   = ChunkData::SIZE;
    constexpr float iso = 0.0f;

    constexpr float COL_W = 64.f/256.f;
    constexpr float ROW_H = 64.f/256.f;
    constexpr float SCALE = 0.5f;
    constexpr float COL_OFFSETS[] = { 0.00f, 0.25f, 0.50f, 0.75f, 0.25f, 0.50f };

    // ── Resize and zero the flat normal accumulator ───────────────────────────
    if ((int)t_normAccum.size() < NORM_SIZE) {
        t_normAccum.resize(NORM_SIZE, {0,0,0});
        t_normUsed.resize(NORM_SIZE, 0);
    }
    // Track which cells we've written so we only zero those (not the full 3.4MB)
    std::vector<int> dirtyNormIds;
    dirtyNormIds.reserve(4096);

    struct RawVert {
        glm::vec3 pos;
        glm::vec2 uv;
        uint32_t  tint;
        int       normId;  // index into t_normAccum
    };

    std::vector<RawVert> rawVerts;
    rawVerts.reserve(8192);

    for(int z=0;z<N;z++) for(int y=0;y<N;y++) for(int x=0;x<N;x++) {
        float vals[8]; uint8_t mats[8]; glm::vec3 pos[8];
        for(int c=0;c<8;c++){
            int cx=x+corners[c].x, cy=y+corners[c].y, cz=z+corners[c].z;
            vals[c]=chunk.values[cx][cy][cz];
            mats[c]=chunk.materials[cx][cy][cz];
            pos[c]=glm::vec3(cx,cy,cz);
        }
        int cubeIndex=0;
        for(int c=0;c<8;c++) if(vals[c]<iso) cubeIndex|=(1<<c);
        if(edgeTable[cubeIndex]==0) continue;

        glm::vec3 edgeVerts[12]; uint8_t edgeMats[12];
        for(int e=0;e<12;e++) if(edgeTable[cubeIndex]&(1<<e)){
            int a=edgePairs[e][0], b=edgePairs[e][1];
            edgeVerts[e]=interp(iso,pos[a],vals[a],pos[b],vals[b]);
            edgeMats[e] =pickMat(iso,vals,mats,a,b);
        }

        for(int t=0; triTable[cubeIndex][t]!=-1; t+=3) {
            int e0=triTable[cubeIndex][t],
                e1=triTable[cubeIndex][t+1],
                e2=triTable[cubeIndex][t+2];
            glm::vec3 v0=edgeVerts[e0], v1=edgeVerts[e1], v2=edgeVerts[e2];
            glm::vec3 cr=glm::cross(v1-v0,v2-v0);
            if(glm::dot(cr,cr)<1e-10f) continue;

            glm::vec3 flatN = glm::normalize(cr);
            uint8_t mat = triMaterial(edgeMats, e0, e1, e2, vals);
            if(mat == (uint8_t)BlockMat::Grass && flatN.y < -1.5f)
                mat = (uint8_t)BlockMat::Dirt;
            uint32_t tint = packNeutralTint(mat);

            glm::vec3 weightedN = cr;
            glm::vec3 triVerts[3] = {v0, v1, v2};

            // UV helper
            auto makeUV = [&](glm::vec3 p) -> glm::vec2 {
                glm::vec3 an = glm::abs(flatN);
                glm::vec2 localUV;
                if      (an.x>an.y && an.x>an.z) localUV = {p.z, p.y};
                else if (an.y>an.z)               localUV = {p.x, p.z};
                else                              localUV = {p.x, p.y};
                float u = std::fmod(std::fabs(localUV.x)*SCALE, 1.0f);
                float v = std::fmod(std::fabs(localUV.y)*SCALE, 1.0f);
                float colOff = (mat<6) ? COL_OFFSETS[mat] : 0.f;
                return {colOff + u*COL_W, v*ROW_H};
            };

            for(int i=0;i<3;i++) {
                int nid = normIdx(triVerts[i]);
                // Track dirty entries for fast reset
                if (!t_normUsed[nid]) {
                    t_normUsed[nid] = 1;
                    t_normAccum[nid] = {0,0,0};
                    dirtyNormIds.push_back(nid);
                }
                t_normAccum[nid] += weightedN;

                rawVerts.push_back({triVerts[i], makeUV(triVerts[i]), tint, nid});
            }
        }
    }

    // ── Build final mesh with smooth normals ──────────────────────────────────
    mesh.vertices.reserve(rawVerts.size());
    mesh.indices.reserve(rawVerts.size());

    for(size_t i=0; i<rawVerts.size(); i++) {
        const RawVert& rv = rawVerts[i];
        glm::vec3 smoothN{0.f, 1.f, 0.f};
        const glm::vec3& acc = t_normAccum[rv.normId];
        float len2 = glm::dot(acc, acc);
        if(len2 > 1e-10f) smoothN = acc / std::sqrt(len2);

        Vertex v;
        v.pos      = rv.pos;
        v.normal   = smoothN;
        v.uv       = rv.uv;
        v.material = rv.tint;
        mesh.vertices.push_back(v);
        mesh.indices.push_back((uint32_t)i);
    }

    // ── Reset only dirty normal entries (not the whole 3.4MB) ─────────────────
    for (int id : dirtyNormIds) t_normUsed[id] = 0;

    return mesh;
}
