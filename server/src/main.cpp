#include "chunk_manager.h"
#include "inventory_manager.h"
#include "config.h"
#include "log.h"
#include "net_common.h"
#include "packets.h"
#include "inv_packets.h"
#include <enet/enet.h>
#include <unordered_map>

// Temporary: use ENet peer pointer as player UID until real auth exists.
// Replace with a proper UUID system when adding accounts.
static uint64_t peerToUID(ENetPeer* peer) {
    return (uint64_t)(uintptr_t)peer;
}

int main(int argc, char **argv) {
    Log::init("aetheris_server.log");
    Log::installCrashHandlers();
    Log::info("Server starting");

    Net::init();
    Net::Host host(Config::SERVER_PORT, 32);

    ChunkManager    chunks(1);
    InventoryManager invMgr;

    // Seed some world chests on first run (only if none loaded from disk)
    // Replace positions with real world coords once terrain is finalized
    // invMgr.addChest({0.f, 80.f, 10.f}, []{ Inventory i; i.add(ItemID::WpnSword,1); return i; }());

    Log::info(std::string("Listening on port ") +
              std::to_string(Config::SERVER_PORT));

    std::unordered_map<ENetPeer*, glm::vec3> positions;

    while (true) {
        ENetEvent ev;
        while (enet_host_service(host.get(), &ev, 0) > 0) {
            switch (ev.type) {

            case ENET_EVENT_TYPE_CONNECT: {
                Log::info("Client connected");
                chunks.addClient(ev.peer);
                invMgr.onPlayerConnect(ev.peer, peerToUID(ev.peer));

                float spawnY = chunks.findSpawnY(0.f, 0.f);
                positions[ev.peer] = {0.f, spawnY, 0.f};

                chunks.updateClient(ev.peer, 0.f, spawnY, 0.f);
                chunks.flushReady(host.get());

                // Send spawn position
                SpawnPositionPacket sp{0.f, spawnY, 0.f};
                Net::sendReliable(ev.peer, sp.serialize());

                // Send authoritative inventory state
                invMgr.sendInventoryState(ev.peer);

                enet_host_flush(host.get());
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                const uint8_t* d   = ev.packet->data;
                size_t         len = ev.packet->dataLength;
                if (len == 0) { enet_packet_destroy(ev.packet); break; }

                uint8_t pid = d[0];

                // ── Chunk / movement packets ──────────────────────────────────
                if (pid == (uint8_t)PacketID::PlayerMove) {
                    auto mv = PlayerMovePacket::deserialize(d, len);
                    glm::vec3 pos{mv.x, mv.y, mv.z};
                    positions[ev.peer] = pos;
                    chunks.updateClient(ev.peer, mv.x, mv.y, mv.z);
                    invMgr.onPlayerMove(ev.peer, pos);

                } else if (pid == (uint8_t)PacketID::RespawnRequest) {
                    float spawnY = chunks.findSpawnY(0.f, 0.f);
                    positions[ev.peer] = {0.f, spawnY, 0.f};

                    chunks.resetClient(ev.peer);
                    chunks.updateClient(ev.peer, 0.f, spawnY, 0.f);

                    SpawnPositionPacket sp{0.f, spawnY, 0.f};
                    Net::sendReliable(ev.peer, sp.serialize());
                    invMgr.sendInventoryState(ev.peer);
                    enet_host_flush(host.get());
                    Log::info(std::string("Respawn at y=") + std::to_string(spawnY));

                // ── Inventory packets ─────────────────────────────────────────
                } else if (pid == (uint8_t)InvPacketID::ChestOpenReq) {
                    auto req = ChestOpenReqPacket::deserialize(d, len);
                    invMgr.onChestOpenReq(ev.peer, req);
                    enet_host_flush(host.get());

                } else if (pid == (uint8_t)InvPacketID::ChestCloseReq) {
                    auto req = ChestCloseReqPacket::deserialize(d, len);
                    invMgr.onChestCloseReq(ev.peer, req);

                } else if (pid == (uint8_t)InvPacketID::InventoryMoveReq) {
                    auto req = InventoryMoveReqPacket::deserialize(d, len);
                    invMgr.onInventoryMoveReq(ev.peer, req);
                    enet_host_flush(host.get());
                }

                enet_packet_destroy(ev.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
                Log::info("Client disconnected");
                chunks.removeClient(ev.peer);
                invMgr.onPlayerDisconnect(ev.peer);
                positions.erase(ev.peer);
                break;

            default:
                break;
            }
        }

        chunks.flushReady(host.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Net::deinit();
    Log::shutdown();
}
