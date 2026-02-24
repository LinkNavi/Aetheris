#include "chunk_manager.h"
#include "noise_gen.h"
#include "marching_cubes.h"
#include "net_common.h"
#include <cmath>
#include <algorithm>

static ChunkCoord worldToChunk(float wx, float wy, float wz) {
    int sz = ChunkData::SIZE;
    return { (int)std::floor(wx/sz), (int)std::floor(wy/sz), (int)std::floor(wz/sz) };
}

ChunkManager::ChunkManager(int genThreads)
    : _pool(genThreads) {}

ClientState* ChunkManager::findClient(ENetPeer* peer) {
    for (auto& c : _clients)
        if (c.peer == peer) return &c;
    return nullptr;
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
    cs->pendingChunks.clear();
    cs->lastChunk = {INT_MIN, INT_MIN, INT_MIN};
}

// ── scheduleChunk ─────────────────────────────────────────────────────────────
// Called on ENet thread. Checks cache; if hit sends immediately, else submits
// a generation task to the thread pool.

void ChunkManager::scheduleChunk(ClientState& cs, ChunkCoord coord) {
    if (cs.sentChunks.count(coord) || cs.pendingChunks.count(coord)) return;

    // Check cache under lock
    {
        std::lock_guard lk(_cacheMu);
        auto it = _cache.find(coord);
        if (it != _cache.end()) {
            // Already generated — push straight to ready queue
            std::lock_guard rlk(_readyMu);
            _ready.push({cs.peer, coord, it->second});
            cs.sentChunks.insert(coord);
            return;
        }
    }

    cs.pendingChunks.insert(coord);
    ENetPeer* peer = cs.peer;

    _pool.submit([this, peer, coord]() {
        generateAndEnqueue(peer, coord);
    });
}

void ChunkManager::generateAndEnqueue(ENetPeer* peer, ChunkCoord coord) {
    // Pure CPU work — no ENet calls here
    ChunkData data = generateChunk(coord);
    ChunkMesh mesh = marchChunk(data);
    auto bytes = ChunkDataPacket::from(mesh).serialize();

    {
        std::lock_guard lk(_cacheMu);
        _cache.emplace(coord, bytes);
    }
    {
        std::lock_guard lk(_readyMu);
        _ready.push({peer, coord, std::move(bytes)});
    }
}

// ── updateClient ──────────────────────────────────────────────────────────────
// Called when a PlayerMove packet arrives. Only schedules new chunks.

void ChunkManager::updateClient(ENetPeer* peer, float wx, float wy, float wz) {
    ClientState* cs = findClient(peer);
    if (!cs) return;

    ChunkCoord center = worldToChunk(wx, wy, wz);
    if (center == cs->lastChunk) return; // didn't cross a chunk boundary
    cs->lastChunk = center;

    for (int dx = -Config::CHUNK_RADIUS_XZ; dx <= Config::CHUNK_RADIUS_XZ; dx++)
    for (int dy = -Config::CHUNK_RADIUS_Y;  dy <= Config::CHUNK_RADIUS_Y;  dy++)
    for (int dz = -Config::CHUNK_RADIUS_XZ; dz <= Config::CHUNK_RADIUS_XZ; dz++)
        scheduleChunk(*cs, {center.x+dx, center.y+dy, center.z+dz});
}

// ── flushReady ────────────────────────────────────────────────────────────────
// Called every server tick from the ENet thread.
// Drains the ready queue and sends packets. ENet is not thread-safe so all
// enet_peer_send calls must happen here, not in the worker threads.

void ChunkManager::flushReady(ENetHost* host) {
    std::queue<ReadyChunk> batch;
    {
        std::lock_guard lk(_readyMu);
        std::swap(batch, _ready);
    }

    bool sent = false;
    while (!batch.empty()) {
        ReadyChunk& rc = batch.front();

        // Mark pendingChunks as sent (peer might be gone — check)
        ClientState* cs = findClient(rc.peer);
        if (cs) {
            cs->pendingChunks.erase(rc.coord);
            cs->sentChunks.insert(rc.coord);
            ENetPacket* pkt = enet_packet_create(
                rc.bytes.data(), rc.bytes.size(), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(rc.peer, 0, pkt);
            sent = true;
        }

        batch.pop();
    }

    if (sent) enet_host_flush(host);
}

// ── findSpawnY ────────────────────────────────────────────────────────────────

float ChunkManager::findSpawnY(float wx, float wz) {
    return sampleSurfaceY(wx, wz);
}
