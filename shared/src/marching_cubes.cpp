#include "marching_cubes.h"
#include "table.h"
#include <array>
#include <unordered_map>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

static glm::vec3 interp(float iso, glm::vec3 p0, float v0, glm::vec3 p1, float v1) {
    return (std::abs(v0-iso) < std::abs(v1-iso)) ? p0 : p1;
}
static uint8_t pickMat(float iso, const float vals[8], const uint8_t mats[8], int a, int b) {
    return (vals[a] < vals[b]) ? mats[a] : mats[b];
}

static inline uint32_t packNeutralTint(uint8_t mat) {
    return (uint32_t)mat | (128u<<8) | (128u<<16) | (128u<<24);
}

struct Vec3Key {
    int x, y, z;
    bool operator==(const Vec3Key& o) const { return x==o.x && y==o.y && z==o.z; }
};
struct Vec3KeyHash {
    size_t operator()(const Vec3Key& k) const {
        size_t h = 0;
        h ^= std::hash<int>{}(k.x) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(k.y) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(k.z) + 0x9e3779b9 + (h<<6) + (h>>2);
        return h;
    }
};

static Vec3Key quantize(glm::vec3 p) {
    return { (int)std::round(p.x * 1024.f),
             (int)std::round(p.y * 1024.f),
             (int)std::round(p.z * 1024.f) };
}

// Pick dominant material across all three edges of a triangle
static uint8_t triMaterial(const uint8_t edgeMats[12], int e0, int e1, int e2,
                            const float vals[8])
{
    uint8_t m0 = edgeMats[e0];
    uint8_t m1 = edgeMats[e1];
    uint8_t m2 = edgeMats[e2];
    if (m0 == m1 || m0 == m2) return m0;
    if (m1 == m2)              return m1;
    // All differ — pick from the edge closest to the iso surface
    auto edgeDensity = [&](int e) {
        int a = edgePairs[e][0], b = edgePairs[e][1];
        return std::min(std::abs(vals[a]), std::abs(vals[b]));
    };
    float d0 = edgeDensity(e0), d1 = edgeDensity(e1), d2 = edgeDensity(e2);
    if (d0 <= d1 && d0 <= d2) return m0;
    if (d1 <= d2)              return m1;
    return m2;
}

ChunkMesh marchChunk(const ChunkData& chunk) {
    ChunkMesh mesh; mesh.coord = chunk.coord;
    constexpr int   N   = ChunkData::SIZE;
    constexpr float iso = 0.0f;

    constexpr float COL_W = 64.f/256.f;
    constexpr float ROW_H = 64.f/256.f;
    constexpr float SCALE = 0.5f;
    constexpr float COL_OFFSETS[] = { 0.00f, 0.25f, 0.50f, 0.75f, 0.25f, 0.50f };

    struct RawVert {
        glm::vec3 pos;
        glm::vec2 uv;
        uint32_t  tint;
    };

    std::vector<RawVert> rawVerts;
    // Map quantized position -> accumulated area-weighted normal
    std::unordered_map<Vec3Key, glm::vec3, Vec3KeyHash> accumNormals;

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

            // Flat normal used ONLY for material/UV decisions
            glm::vec3 flatN = glm::normalize(cr);

            // Material: majority vote across all three edges
            uint8_t mat = triMaterial(edgeMats, e0, e1, e2, vals);

            // Grass -> dirt only on faces that are clearly pointing sideways.
            // Use a low threshold (0.0) so only truly horizontal-or-below
            // faces lose their grass. Upward-sloping faces keep grass.
            // This was the main bug: smooth normals pulled flatN.y down
            // even for mostly-upward faces, so everything became dirt.
           if(mat == (uint8_t)BlockMat::Grass && flatN.y < -1.5f)
    mat = (uint8_t)BlockMat::Dirt;

            uint32_t tint = packNeutralTint(mat);

            // Area-weighted normal (not normalized) for smooth normal accumulation
            glm::vec3 weightedN = cr;

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

            glm::vec3 triVerts[3] = {v0, v1, v2};
            for(int i=0;i<3;i++) {
                RawVert rv;
                rv.pos  = triVerts[i];
                rv.uv   = makeUV(triVerts[i]);
                rv.tint = tint;
                rawVerts.push_back(rv);

                Vec3Key key = quantize(triVerts[i]);
                auto it = accumNormals.find(key);
                if(it == accumNormals.end())
                    accumNormals[key] = weightedN;
                else
                    it->second += weightedN;
            }
        }
    }

    // ── Pass 2: build final mesh with smooth normals ───────────────────────────
    mesh.vertices.reserve(rawVerts.size());
    mesh.indices.reserve(rawVerts.size());

    for(size_t i=0; i<rawVerts.size(); i++) {
        const RawVert& rv = rawVerts[i];
        Vec3Key key = quantize(rv.pos);

        glm::vec3 smoothN{0.f, 1.f, 0.f};
        auto it = accumNormals.find(key);
        if(it != accumNormals.end()) {
            float len2 = glm::dot(it->second, it->second);
            if(len2 > 1e-10f)
                smoothN = it->second / std::sqrt(len2);
        }

        Vertex v;
        v.pos      = rv.pos;
        v.normal   = smoothN;
        v.uv       = rv.uv;
        v.material = rv.tint;
        mesh.vertices.push_back(v);
        mesh.indices.push_back((uint32_t)i);
    }

    return mesh;
}
