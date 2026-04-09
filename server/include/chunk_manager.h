#pragma once
#include <enet/enet.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <mutex>
#include <climits>
#include <algorithm>
#include <glm/vec3.hpp>
#include "chunk.h"
#include "packets.h"
#include "config.h"
#include "thread_pool.h"
#include "tree_system.h"

struct ClientState {
    ENetPeer*  peer      = nullptr;
    ChunkCoord lastChunk = {INT_MIN, INT_MIN, INT_MIN};
    int        renderDistXZ = Config::CHUNK_RADIUS_XZ;
    int        renderDistY  = Config::CHUNK_RADIUS_Y;
    std::unordered_set<ChunkCoord, ChunkCoordHash> sentChunks;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingChunks;
};

struct ReadyChunk {
    ENetPeer*            peer;
    ChunkCoord           coord;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> treeBytes;
};

class ChunkManager {
public:
    explicit ChunkManager(int genThreads = 0);

    void addClient   (ENetPeer* peer);
    void removeClient(ENetPeer* peer);
    void resetClient (ENetPeer* peer);
void setTreeSystem(TreeSystem* ts) { _treeSys = ts; }
    void updateClient(ENetPeer* peer, float wx, float wy, float wz);

    void flushReady(ENetHost* host);

    float      findSpawnY  (float wx, float wz);   // kept for compat
    glm::vec3  findSpawnPos();                      // searches for land spawn

    void setClientRenderDist(ENetPeer* peer, int xz, int y) {
        ClientState* cs = findClient(peer);
        if (!cs) return;
        cs->renderDistXZ = std::clamp(xz, 1, 255);
        cs->renderDistY  = std::clamp(y,  1, 32);
        cs->lastChunk = {INT_MIN, INT_MIN, INT_MIN};
    }

private:
    ThreadPool _pool;

    mutable std::mutex _cacheMu;
    std::unordered_map<ChunkCoord, std::vector<uint8_t>, ChunkCoordHash> _cache;
TreeSystem* _treeSys = nullptr;
    std::mutex _readyMu;
    std::queue<ReadyChunk> _ready;

    std::vector<ClientState> _clients;

    ClientState* findClient(ENetPeer* peer);
    void         scheduleChunk(ClientState& cs, ChunkCoord coord);
    void         generateAndEnqueue(ENetPeer* peer, ChunkCoord coord);
};
