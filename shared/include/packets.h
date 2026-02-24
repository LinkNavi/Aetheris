#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include "chunk.h"
#include <string>
// Packet IDs
enum class PacketID : uint8_t {
    ChunkData    = 0x01,
    PlayerMove   = 0x02,
    PlayerJoin   = 0x03,
    PlayerLeave  = 0x04,
    SpawnPosition = 0x05
};

// ── Serialization helpers ─────────────────────────────────────────────────────

inline void writeU8 (std::vector<uint8_t>& b, uint8_t  v) { b.push_back(v); }
inline void writeU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((v>>24)&0xFF); b.push_back((v>>16)&0xFF);
    b.push_back((v>> 8)&0xFF); b.push_back( v     &0xFF);
}
inline void writeF32(std::vector<uint8_t>& b, float v) {
    uint32_t tmp; memcpy(&tmp, &v, 4); writeU32(b, tmp);
}
inline void writeI32(std::vector<uint8_t>& b, int32_t v) { writeU32(b, (uint32_t)v); }

inline uint8_t  readU8 (const uint8_t* d, size_t& o) { return d[o++]; }
inline uint32_t readU32(const uint8_t* d, size_t& o) {
    uint32_t v = ((uint32_t)d[o]<<24)|((uint32_t)d[o+1]<<16)|
                 ((uint32_t)d[o+2]<< 8)|((uint32_t)d[o+3]);
    o+=4; return v;
}
inline float   readF32(const uint8_t* d, size_t& o) {
    uint32_t tmp = readU32(d,o); float v; memcpy(&v,&tmp,4); return v;
}
inline int32_t readI32(const uint8_t* d, size_t& o) { return (int32_t)readU32(d,o); }

// ── Packets ───────────────────────────────────────────────────────────────────

struct ChunkDataPacket {
    ChunkCoord coord;
    std::vector<float>    vertices; // interleaved: x,y,z,nx,ny,nz
    std::vector<uint32_t> indices;

    static ChunkDataPacket from(const ChunkMesh& mesh) {
        ChunkDataPacket p;
        p.coord = mesh.coord;
        for (auto& v : mesh.vertices) {
            p.vertices.insert(p.vertices.end(),
                {v.pos.x, v.pos.y, v.pos.z, v.normal.x, v.normal.y, v.normal.z});
        }
        p.indices = mesh.indices;
        return p;
    }

    ChunkMesh toMesh() const {
        ChunkMesh m;
        m.coord = coord;
        for (size_t i = 0; i+5 < vertices.size(); i+=6)
            m.vertices.push_back({{vertices[i],vertices[i+1],vertices[i+2]},
                                  {vertices[i+3],vertices[i+4],vertices[i+5]}});
        m.indices = indices;
        return m;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)PacketID::ChunkData);
        writeI32(b, coord.x); writeI32(b, coord.y); writeI32(b, coord.z);
        writeU32(b, vertices.size());
        for (float f : vertices) writeF32(b, f);
        writeU32(b, indices.size());
        for (uint32_t i : indices) writeU32(b, i);
        return b;
    }

    static ChunkDataPacket deserialize(const uint8_t* d, size_t len) {
        ChunkDataPacket p; size_t o = 1; // skip packet id
        p.coord.x = readI32(d,o); p.coord.y = readI32(d,o); p.coord.z = readI32(d,o);
        uint32_t vc = readU32(d,o); p.vertices.resize(vc);
        for (auto& f : p.vertices) f = readF32(d,o);
        uint32_t ic = readU32(d,o); p.indices.resize(ic);
        for (auto& i : p.indices) i = readU32(d,o);
        return p;
    }
};

struct PlayerMovePacket {
    float x, y, z;
    float yaw, pitch;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)PacketID::PlayerMove);
        writeF32(b,x); writeF32(b,y); writeF32(b,z);
        writeF32(b,yaw); writeF32(b,pitch);
        return b;
    }

    static PlayerMovePacket deserialize(const uint8_t* d, size_t) {
        PlayerMovePacket p; size_t o = 1;
        p.x=readF32(d,o); p.y=readF32(d,o); p.z=readF32(d,o);
        p.yaw=readF32(d,o); p.pitch=readF32(d,o);
        return p;
    }
};

struct PlayerJoinPacket {
    std::string name;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)PacketID::PlayerJoin);
        writeU32(b, name.size());
        b.insert(b.end(), name.begin(), name.end());
        return b;
    }

    static PlayerJoinPacket deserialize(const uint8_t* d, size_t) {
        PlayerJoinPacket p; size_t o = 1;
        uint32_t len = readU32(d,o);
        p.name.assign((const char*)d+o, len);
        return p;
    }
};

struct SpawnPositionPacket {
    float x, y, z;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)PacketID::SpawnPosition);
        writeF32(b,x); writeF32(b,y); writeF32(b,z);
        return b;
    }

    static SpawnPositionPacket deserialize(const uint8_t* d, size_t) {
        SpawnPositionPacket p; size_t o = 1;
        p.x=readF32(d,o); p.y=readF32(d,o); p.z=readF32(d,o);
        return p;
    }
};
