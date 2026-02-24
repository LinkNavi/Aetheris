#pragma once
#include <enet/enet.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <climits>
#include "chunk.h"
#include "packets.h"
#include "config.h"

struct ClientState {
    ENetPeer*  peer      = nullptr;
    ChunkCoord lastChunk = {INT_MIN, INT_MIN, INT_MIN};
    std::unordered_set<ChunkCoord, ChunkCoordHash> sentChunks;
};

class ChunkManager {
public:
    void addClient(ENetPeer* peer);
    void removeClient(ENetPeer* peer);
    void updateClient(ENetPeer* peer, float wx, float wy, float wz);
    void sendInitialChunks(ENetPeer* peer, float wx, float wy, float wz,
                           int radiusXZ, int radiusY);
    float findSpawnY(float wx, float wz);

private:
    std::vector<ClientState> _clients;
    std::unordered_map<ChunkCoord, std::vector<uint8_t>, ChunkCoordHash> _cache;

    ClientState*          findClient(ENetPeer* peer);
    std::vector<uint8_t>& getOrGenChunk(ChunkCoord coord);
};
