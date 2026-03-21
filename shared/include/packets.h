#pragma once
#include "chunk.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

enum class PacketID : uint8_t {
  ChunkData = 0x01,
  PlayerMove = 0x02,
  PlayerJoin = 0x03,
  PlayerLeave = 0x04,
  SpawnPosition = 0x05,
  RespawnRequest = 0x06,
  RenderDist = 0x07,
  TreeSpawn = 0x08,
  WaterChunkData = 0x09,
  WaterPlace     = 0x0A,
  SpawnTree      = 0x0B,
 WorldTime = 0x0C,
};

inline void writeU8(std::vector<uint8_t> &b, uint8_t v) { b.push_back(v); }
inline void writeU32(std::vector<uint8_t> &b, uint32_t v) {
  b.push_back((v >> 24) & 0xFF); b.push_back((v >> 16) & 0xFF);
  b.push_back((v >> 8) & 0xFF);  b.push_back(v & 0xFF);
}
inline void writeF32(std::vector<uint8_t> &b, float v) {
  uint32_t tmp; memcpy(&tmp, &v, 4); writeU32(b, tmp);
}
inline void writeI32(std::vector<uint8_t> &b, int32_t v) { writeU32(b, (uint32_t)v); }

inline uint8_t readU8(const uint8_t *d, size_t &o) { return d[o++]; }
inline uint32_t readU32(const uint8_t *d, size_t &o) {
  uint32_t v = ((uint32_t)d[o] << 24) | ((uint32_t)d[o + 1] << 16) |
               ((uint32_t)d[o + 2] << 8) | ((uint32_t)d[o + 3]);
  o += 4; return v;
}
inline float readF32(const uint8_t *d, size_t &o) {
  uint32_t tmp = readU32(d, o); float v; memcpy(&v, &tmp, 4); return v;
}
inline int32_t readI32(const uint8_t *d, size_t &o) { return (int32_t)readU32(d, o); }

struct ChunkDataPacket {
  ChunkCoord coord;
  std::vector<float> vertices;
  std::vector<uint32_t> materials;
  std::vector<uint32_t> indices;

  static ChunkDataPacket from(const ChunkMesh &mesh) {
    ChunkDataPacket p; p.coord = mesh.coord;
    p.vertices.reserve(mesh.vertices.size() * 8);
    p.materials.reserve(mesh.vertices.size());
    for (auto &v : mesh.vertices) {
      p.vertices.push_back(v.pos.x); p.vertices.push_back(v.pos.y); p.vertices.push_back(v.pos.z);
      p.vertices.push_back(v.normal.x); p.vertices.push_back(v.normal.y); p.vertices.push_back(v.normal.z);
      p.vertices.push_back(v.uv.x); p.vertices.push_back(v.uv.y);
      p.materials.push_back((uint32_t)v.material);
    }
    p.indices = mesh.indices; return p;
  }

  ChunkMesh toMesh() const {
    ChunkMesh m; m.coord = coord;
    size_t vertCount = vertices.size() / 8;
    m.vertices.reserve(vertCount);
    for (size_t i = 0; i < vertCount; i++) {
      size_t b = i * 8; Vertex v;
      v.pos = {vertices[b], vertices[b+1], vertices[b+2]};
      v.normal = {vertices[b+3], vertices[b+4], vertices[b+5]};
      v.uv = {vertices[b+6], vertices[b+7]};
      v.material = (i < materials.size()) ? materials[i] : 0u;
      m.vertices.push_back(v);
    }
    m.indices = indices; return m;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    writeU8(b, (uint8_t)PacketID::ChunkData);
    writeI32(b, coord.x); writeI32(b, coord.y); writeI32(b, coord.z);
    writeU32(b, (uint32_t)vertices.size());
    for (float f : vertices) writeF32(b, f);
    writeU32(b, (uint32_t)materials.size());
    for (uint32_t mat : materials) writeU32(b, mat);
    writeU32(b, (uint32_t)indices.size());
    for (uint32_t i : indices) writeU32(b, i);
    return b;
  }

  static ChunkDataPacket deserialize(const uint8_t *d, size_t len) {
    ChunkDataPacket p; size_t o = 1;
    p.coord.x = readI32(d, o); p.coord.y = readI32(d, o); p.coord.z = readI32(d, o);
    uint32_t vc = readU32(d, o); p.vertices.resize(vc);
    for (auto &f : p.vertices) f = readF32(d, o);
    uint32_t mc = readU32(d, o); p.materials.resize(mc);
    for (auto &mat : p.materials) mat = readU32(d, o);
    uint32_t ic = readU32(d, o); p.indices.resize(ic);
    for (auto &i : p.indices) i = readU32(d, o);
    return p;
  }
};

struct PlayerMovePacket {
  float x, y, z, yaw, pitch;
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::PlayerMove);
    writeF32(b, x); writeF32(b, y); writeF32(b, z); writeF32(b, yaw); writeF32(b, pitch); return b;
  }
  static PlayerMovePacket deserialize(const uint8_t *d, size_t) {
    PlayerMovePacket p; size_t o = 1;
    p.x = readF32(d, o); p.y = readF32(d, o); p.z = readF32(d, o);
    p.yaw = readF32(d, o); p.pitch = readF32(d, o); return p;
  }
};

struct PlayerJoinPacket {
  std::string name;
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::PlayerJoin);
    writeU32(b, name.size()); b.insert(b.end(), name.begin(), name.end()); return b;
  }
  static PlayerJoinPacket deserialize(const uint8_t *d, size_t) {
    PlayerJoinPacket p; size_t o = 1; uint32_t len = readU32(d, o);
    p.name.assign((const char *)d + o, len); return p;
  }
};

struct SpawnPositionPacket {
  float x, y, z;
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::SpawnPosition);
    writeF32(b, x); writeF32(b, y); writeF32(b, z); return b;
  }
  static SpawnPositionPacket deserialize(const uint8_t *d, size_t) {
    SpawnPositionPacket p; size_t o = 1;
    p.x = readF32(d, o); p.y = readF32(d, o); p.z = readF32(d, o); return p;
  }
};

struct RespawnRequestPacket {
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::RespawnRequest); return b;
  }
};

struct TreeSpawnPacket {
  struct Entry { float wx, wy, wz, yaw, scale; uint8_t templateIdx; };
  std::vector<Entry> trees;
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::TreeSpawn);
    writeU32(b, (uint32_t)trees.size());
    for (auto &t : trees) {
      writeF32(b, t.wx); writeF32(b, t.wy); writeF32(b, t.wz);
      writeF32(b, t.yaw); writeF32(b, t.scale); writeU8(b, t.templateIdx);
    } return b;
  }
  static TreeSpawnPacket deserialize(const uint8_t *d, size_t) {
    TreeSpawnPacket p; size_t o = 1; uint32_t cnt = readU32(d, o); p.trees.resize(cnt);
    for (auto &t : p.trees) {
      t.wx = readF32(d, o); t.wy = readF32(d, o); t.wz = readF32(d, o);
      t.yaw = readF32(d, o); t.scale = readF32(d, o); t.templateIdx = readU8(d, o);
    } return p;
  }
};

struct RenderDistPacket {
  uint8_t xz, y;
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::RenderDist);
    writeU8(b, xz); writeU8(b, y); return b;
  }
  static RenderDistPacket deserialize(const uint8_t *d, size_t) {
    RenderDistPacket p; size_t o = 1; p.xz = readU8(d, o); p.y = readU8(d, o); return p;
  }
};

// Bit-packed water: 4096 bytes per chunk
struct WaterChunkDataPacket {
    ChunkCoord coord;
    static constexpr int DATA_SIZE = ChunkData::SIZE * ChunkData::SIZE * ChunkData::SIZE / 8;
    std::array<uint8_t, DATA_SIZE> bits{};

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)PacketID::WaterChunkData);
        writeI32(b, coord.x); writeI32(b, coord.y); writeI32(b, coord.z);
        b.insert(b.end(), bits.begin(), bits.end());
        return b;
    }
    static WaterChunkDataPacket deserialize(const uint8_t* d, size_t) {
        WaterChunkDataPacket p; size_t o = 1;
        p.coord.x = readI32(d, o); p.coord.y = readI32(d, o); p.coord.z = readI32(d, o);
        for (auto& v : p.bits) v = readU8(d, o);
        return p;
    }
};

struct WaterPlacePacket {
    int32_t wx, wy, wz; uint8_t level;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::WaterPlace);
        writeI32(b, wx); writeI32(b, wy); writeI32(b, wz); writeU8(b, level); return b;
    }
    static WaterPlacePacket deserialize(const uint8_t* d, size_t) {
        WaterPlacePacket p; size_t o = 1;
        p.wx = readI32(d, o); p.wy = readI32(d, o); p.wz = readI32(d, o);
        p.level = readU8(d, o); return p;
    }
};

struct SpawnTreePacket {
    float wx, wy, wz;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b; writeU8(b, (uint8_t)PacketID::SpawnTree);
        writeF32(b, wx); writeF32(b, wy); writeF32(b, wz); return b;
    }
    static SpawnTreePacket deserialize(const uint8_t* d, size_t) {
        SpawnTreePacket p; size_t o = 1;
        p.wx = readF32(d, o); p.wy = readF32(d, o); p.wz = readF32(d, o); return p;
    }
};

struct WorldTimePacket {
    float time;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)PacketID::WorldTime);
        writeF32(b, time);
        return b;
    }
    static WorldTimePacket deserialize(const uint8_t* d, size_t) {
        WorldTimePacket p; size_t o = 1;
        p.time = readF32(d, o);
        return p;
    }
};
