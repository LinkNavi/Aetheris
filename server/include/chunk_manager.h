#pragma once
#include <enet/enet.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <mutex>
#include <climits>
#include "chunk.h"
#include "packets.h"
#include "config.h"
#include "thread_pool.h"

struct ClientState {
    ENetPeer*  peer      = nullptr;
    ChunkCoord lastChunk = {INT_MIN, INT_MIN, INT_MIN};
    std::unordered_set<ChunkCoord, ChunkCoordHash> sentChunks;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingChunks;
};

struct ReadyChunk {
    ENetPeer*            peer;
    ChunkCoord           coord;
    std::vector<uint8_t> bytes;
};

class ChunkManager {
public:
    explicit ChunkManager(int genThreads = 0);

    void addClient   (ENetPeer* peer);
    void removeClient(ENetPeer* peer);
    void resetClient (ENetPeer* peer);

    void updateClient(ENetPeer* peer, float wx, float wy, float wz);

    // Call every server tick from the ENet thread — sends finished chunks
    void flushReady(ENetHost* host);

    float findSpawnY(float wx, float wz);

private:
    ThreadPool _pool;

    std::mutex _cacheMu;
    std::unordered_map<ChunkCoord, std::vector<uint8_t>, ChunkCoordHash> _cache;

    std::mutex _readyMu;
    std::queue<ReadyChunk> _ready;

    std::vector<ClientState> _clients;

    ClientState* findClient(ENetPeer* peer);
    void         scheduleChunk(ClientState& cs, ChunkCoord coord);
    void         generateAndEnqueue(ENetPeer* peer, ChunkCoord coord);
};
