#include "mesh_builder.h"

MeshBuilder::MeshBuilder(int nThreads)
    : _pool(nThreads) {}

void MeshBuilder::submit(const uint8_t* data, size_t len) {
    // Copy bytes so caller can free the packet immediately
    std::vector<uint8_t> buf(data, data + len);
    _inFlight.fetch_add(1, std::memory_order_relaxed);

    _pool.submit([this, buf = std::move(buf)]() {
        auto pkt  = ChunkDataPacket::deserialize(buf.data(), buf.size());
        auto mesh = pkt.toMesh();

        {
            std::lock_guard lk(_readyMu);
            _ready.push(std::move(mesh));
        }
        _inFlight.fetch_sub(1, std::memory_order_relaxed);
    });
}

int MeshBuilder::poll(std::vector<ChunkMesh>& out, int maxPerFrame) {
    std::lock_guard lk(_readyMu);
    int n = 0;
    while (!_ready.empty() && n < maxPerFrame) {
        out.push_back(std::move(_ready.front()));
        _ready.pop();
        n++;
    }
    return n;
}

int MeshBuilder::pending() const {
    return _inFlight.load(std::memory_order_relaxed);
}
