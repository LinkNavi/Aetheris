#include "chunk_manager.h"
#include "config.h"
#include "net_common.h"
#include "packets.h"
#include <enet/enet.h>
#include <iostream>
#include <unordered_map>

int main() {
    Net::init();
    Net::Host host(Config::SERVER_PORT, 32);
    ChunkManager chunks;

    std::cout << "[server] listening on port " << Config::SERVER_PORT << "\n";

    std::unordered_map<ENetPeer*, glm::vec3> positions;

    while (true) {
        ENetEvent ev;
        while (enet_host_service(host.get(), &ev, 16) > 0) {
            switch (ev.type) {

            case ENET_EVENT_TYPE_CONNECT: {
                std::cout << "[server] client connected\n";
                chunks.addClient(ev.peer);

                float spawnY = chunks.findSpawnY(0.f, 0.f);
                positions[ev.peer] = {0.f, spawnY, 0.f};

                chunks.sendInitialChunks(ev.peer, 0.f, spawnY, 0.f, 2, 2);
                enet_host_flush(host.get());

                SpawnPositionPacket sp{0.f, spawnY, 0.f};
                Net::sendReliable(ev.peer, sp.serialize());
                enet_host_flush(host.get());
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                const uint8_t* d = ev.packet->data;
                size_t len       = ev.packet->dataLength;

                if (len > 0 && d[0] == (uint8_t)PacketID::PlayerMove) {
                    auto mv = PlayerMovePacket::deserialize(d, len);
                    positions[ev.peer] = {mv.x, mv.y, mv.z};
                    chunks.updateClient(ev.peer, mv.x, mv.y, mv.z);
                }
                else if (len > 0 && d[0] == (uint8_t)PacketID::RespawnRequest) {
                    float spawnY = chunks.findSpawnY(0.f, 0.f);
                    positions[ev.peer] = {0.f, spawnY, 0.f};
                    SpawnPositionPacket sp{0.f, spawnY, 0.f};
                    Net::sendReliable(ev.peer, sp.serialize());
                    enet_host_flush(host.get());
                    std::cout << "[server] respawn at y=" << spawnY << "\n";
                }

                enet_packet_destroy(ev.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "[server] client disconnected\n";
                chunks.removeClient(ev.peer);
                positions.erase(ev.peer);
                break;

            default: break;
            }
        }
    }

    Net::deinit();
}
