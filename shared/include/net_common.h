#pragma once
#include <enet/enet.h>
#include <stdexcept>
#include <vector>
#include <cstdint>

namespace Net {

inline void init() {
    if (enet_initialize() != 0)
        throw std::runtime_error("enet_initialize failed");
}

inline void deinit() {
    enet_deinitialize();
}

// Wraps an ENetHost with RAII
struct Host {
    ENetHost* h = nullptr;

    // Server ctor
    Host(uint16_t port, size_t maxClients) {
        ENetAddress addr{};
        addr.host = ENET_HOST_ANY;
        addr.port = port;
        h = enet_host_create(&addr, maxClients, 2, 0, 0);
        if (!h) throw std::runtime_error("enet_host_create (server) failed");
    }

    // Client ctor
    explicit Host() {
        h = enet_host_create(nullptr, 1, 2, 0, 0);
        if (!h) throw std::runtime_error("enet_host_create (client) failed");
    }

    ~Host() { if (h) enet_host_destroy(h); }

    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    ENetHost* operator->() { return h; }
    ENetHost* get()        { return h; }
};

// Send a raw byte vector on channel 0, reliable
inline void sendReliable(ENetPeer* peer, const std::vector<uint8_t>& data) {
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(),
                                         ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, pkt);
}

} // namespace Net
