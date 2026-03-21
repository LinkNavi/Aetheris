#include "marching_cubes.h"
#include "table.h"
#include <array>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

static glm::vec3 interp(float iso, glm::vec3 p0, float v0, glm::vec3 p1, float v1) {
    return (std::abs(v0-iso) < std::abs(v1-iso)) ? p0 : p1;
}
static uint8_t pickMat(float iso, const float vals[8], const uint8_t mats[8], int a, int b) {
    return (vals[a] < vals[b]) ? mats[a] : mats[b];
}

// Encode material + neutral tint (128,128,128) into the packed uint32.
// Biome tint is now baked at chunk-gen time via the material value itself —
// removing the per-triangle sampleSurfaceY call that was killing CPU performance.
static inline uint32_t packNeutralTint(uint8_t mat) {
    return (uint32_t)mat | (128u<<8) | (128u<<16) | (128u<<24);
}

ChunkMesh marchChunk(const ChunkData& chunk) {
    ChunkMesh mesh; mesh.coord=chunk.coord;
    constexpr int   N   = ChunkData::SIZE;
    constexpr float iso = 0.0f;

    constexpr float COL_W = 64.f/256.f;
    constexpr float ROW_H = 64.f/256.f;
    constexpr float SCALE = 0.5f;
    constexpr float COL_OFFSETS[] = { 0.00f, 0.25f, 0.50f, 0.75f, 0.25f, 0.50f };

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
            int e0=triTable[cubeIndex][t], e1=triTable[cubeIndex][t+1], e2=triTable[cubeIndex][t+2];
            glm::vec3 v0=edgeVerts[e0], v1=edgeVerts[e1], v2=edgeVerts[e2];
            glm::vec3 cr=glm::cross(v1-v0,v2-v0);
            if(glm::dot(cr,cr)<1e-10f) continue;
            glm::vec3 normal=glm::normalize(cr);

            uint8_t mat=edgeMats[e0];
            if(mat==(uint8_t)BlockMat::Grass && normal.y<0.3f)
                mat=(uint8_t)BlockMat::Dirt;

            uint32_t tint = packNeutralTint(mat);

            auto makeVertex = [&](glm::vec3 p) -> Vertex {
                glm::vec3 an=glm::abs(normal);
                glm::vec2 localUV;
                if      (an.x>an.y && an.x>an.z) localUV={p.z, p.y};
                else if (an.y>an.z)               localUV={p.x, p.z};
                else                              localUV={p.x, p.y};
                float u=std::fmod(std::fabs(localUV.x)*SCALE,1.0f);
                float v=std::fmod(std::fabs(localUV.y)*SCALE,1.0f);
                float colOff=(mat<6)?COL_OFFSETS[mat]:0.f;
                return {p, normal, {colOff+u*COL_W, v*ROW_H}, tint};
            };

            uint32_t base=(uint32_t)mesh.vertices.size();
            mesh.vertices.push_back(makeVertex(v0));
            mesh.vertices.push_back(makeVertex(v1));
            mesh.vertices.push_back(makeVertex(v2));
            mesh.indices.push_back(base);
            mesh.indices.push_back(base+1);
            mesh.indices.push_back(base+2);
        }
    }
    return mesh;
}
