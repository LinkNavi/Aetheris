#include "chunk_manager.h"
#include "noise_gen.h"
#include "marching_cubes.h"
#include "net_common.h"
#include <cmath>
#include <algorithm>

static ChunkCoord worldToChunk(float wx, float wy, float wz) {
    int sz = ChunkData::SIZE;
    return {
        (int)std::floor(wx / sz),
        (int)std::floor(wy / sz),
        (int)std::floor(wz / sz)
    };
}

ClientState* ChunkManager::findClient(ENetPeer* peer) {
    for (auto& c : _clients)
        if (c.peer == peer) return &c;
    return nullptr;
}

std::vector<uint8_t>& ChunkManager::getOrGenChunk(ChunkCoord coord) {
    auto it = _cache.find(coord);
    if (it != _cache.end()) return it->second;
    ChunkData data = generateChunk(coord);
    ChunkMesh mesh = marchChunk(data);
    _cache[coord]  = ChunkDataPacket::from(mesh).serialize();
    return _cache[coord];
}

void ChunkManager::addClient(ENetPeer* peer) {
    _clients.push_back({peer});
}

void ChunkManager::removeClient(ENetPeer* peer) {
    _clients.erase(
        std::remove_if(_clients.begin(), _clients.end(),
            [peer](const ClientState& c){ return c.peer == peer; }),
        _clients.end());
}

void ChunkManager::resetClient(ENetPeer* peer) {
    ClientState* cs = findClient(peer);
    if (!cs) return;
    cs->sentChunks.clear();
    cs->lastChunk = {INT_MIN, INT_MIN, INT_MIN};
}

void ChunkManager::sendInitialChunks(ENetPeer* peer, float wx, float wy, float wz,
                                      int radiusXZ, int radiusY) {
    ClientState* cs = findClient(peer);
    if (!cs) return;
    ChunkCoord center = worldToChunk(wx, wy, wz);
    for (int dx = -radiusXZ; dx <= radiusXZ; dx++)
    for (int dy = -radiusY;  dy <= radiusY;  dy++)
    for (int dz = -radiusXZ; dz <= radiusXZ; dz++) {
        ChunkCoord cc{center.x+dx, center.y+dy, center.z+dz};
        if (cs->sentChunks.count(cc)) continue;
        auto& bytes = getOrGenChunk(cc);
        ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(),
                                              ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, 0, pkt);
        cs->sentChunks.insert(cc);
    }
}

void ChunkManager::updateClient(ENetPeer* peer, float wx, float wy, float wz) {
    ClientState* cs = findClient(peer);
    if (!cs) return;
    ChunkCoord center = worldToChunk(wx, wy, wz);
    for (int dx = -Config::CHUNK_RADIUS_XZ; dx <= Config::CHUNK_RADIUS_XZ; dx++)
    for (int dy = -Config::CHUNK_RADIUS_Y;  dy <= Config::CHUNK_RADIUS_Y;  dy++)
    for (int dz = -Config::CHUNK_RADIUS_XZ; dz <= Config::CHUNK_RADIUS_XZ; dz++) {
        ChunkCoord cc{center.x+dx, center.y+dy, center.z+dz};
        if (cs->sentChunks.count(cc)) continue;
        auto& bytes = getOrGenChunk(cc);
        ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(),
                                              ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, 0, pkt);
        cs->sentChunks.insert(cc);
    }
    cs->lastChunk = center;
}

float ChunkManager::findSpawnY(float wx, float wz) {
    constexpr int N = ChunkData::SIZE;
    for (int worldY = 200; worldY > -100; worldY--) {
        int cy  = (int)std::floor((float)worldY       / N);
        int cy1 = (int)std::floor((float)(worldY - 1) / N);

        ChunkCoord cc { (int)std::floor(wx/N), cy,  (int)std::floor(wz/N) };
        ChunkCoord cc1{ (int)std::floor(wx/N), cy1, (int)std::floor(wz/N) };

        ChunkData d0 = generateChunk(cc);
        ChunkData d1 = generateChunk(cc1);

        int lx  = ((int)wx % N + N) % N;
        int lz  = ((int)wz % N + N) % N;
        int ly0 = ((worldY     % N) + N) % N;
        int ly1 = (((worldY-1) % N) + N) % N;

        float v0 = d0.values[lx][ly0][lz];
        float v1 = d1.values[lx][ly1][lz];

        if (v0 >= 0.f && v1 < 0.f) {
            return (float)worldY + Config::PLAYER_HEIGHT + 1.f;
        }
    }
    return 60.f;
}
