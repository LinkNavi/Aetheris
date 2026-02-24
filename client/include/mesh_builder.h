#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include "chunk.h"
#include "packets.h"
#include "thread_pool.h"

// Receives raw ChunkDataPacket bytes from the network thread,
// deserializes + runs toMesh() on a worker thread, then exposes
// finished ChunkMesh objects for the main thread to poll.
//
// Thread model:
//   Main/ENet thread  →  submit(bytes)      (fast, just a queue push)
//   Worker thread     →  deserialize+toMesh (CPU heavy, off main)
//   Main thread       →  poll(mesh)          (non-blocking drain)

class MeshBuilder {
public:
    // nThreads=0 → auto (hardware_concurrency - 1, min 1)
    explicit MeshBuilder(int nThreads = 0);

    // Push raw packet bytes. Copies the data — safe to call right before
    // enet_packet_destroy(). Non-blocking.
    void submit(const uint8_t* data, size_t len);

    // Drain up to maxPerFrame finished meshes into out[].
    // Returns number of meshes written. Non-blocking.
    int poll(std::vector<ChunkMesh>& out, int maxPerFrame = 8);

    // How many jobs are still in flight (for loading screen etc.)
    int pending() const;

private:
    ThreadPool _pool;

    mutable std::mutex _readyMu;
    std::queue<ChunkMesh> _ready;

    std::atomic<int> _inFlight{0};
};
