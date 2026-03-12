#include "chunk_manager.h"
#include "inventory_manager.h"
#include "stats_manager.h"
#include "multiplayer_manager.h"
#include "config.h"
#include "log.h"
#include "net_common.h"
#include "packets.h"
#include "inv_packets.h"
#include "mp_packets.h"
#include "player_stats.h"
#include <enet/enet.h>
#include <unordered_map>
#include <chrono>

static uint64_t peerToUID(ENetPeer* peer) {
    return (uint64_t)(uintptr_t)peer;
}

int main(int argc, char **argv) {
    Log::init("aetheris_server.log");
    Log::installCrashHandlers();
    Log::info("Server starting");

    Net::init();
    Net::Host host(Config::SERVER_PORT, 32);

    ChunkManager     chunks(1);
    InventoryManager invMgr;
    StatsManager     statsMgr;
    MultiplayerManager mpMgr;

    // Parse auth server config from args: --auth-host X --auth-port Y
    for (int i = 1; i + 1 < argc; i++) {
        if (std::string(argv[i]) == "--auth-host") mpMgr.authHost = argv[++i];
        else if (std::string(argv[i]) == "--auth-port") mpMgr.authPort = std::atoi(argv[++i]);
    }
    Log::info("Auth server: " + mpMgr.authHost + ":" + std::to_string(mpMgr.authPort));

    Log::info(std::string("Listening on port ") +
              std::to_string(Config::SERVER_PORT));

    std::unordered_map<ENetPeer*, glm::vec3> positions;

    using Clock = std::chrono::steady_clock;
    auto lastTick = Clock::now();
    float statsFlushAccum = 0.f;
    float possBroadcastAccum = 0.f;

    while (true) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;
        if (dt > 0.1f) dt = 0.1f;

        ENetEvent ev;
        while (enet_host_service(host.get(), &ev, 0) > 0) {
            switch (ev.type) {

            case ENET_EVENT_TYPE_CONNECT: {
                Log::info("Peer connected (awaiting auth)");
                mpMgr.onPeerConnect(ev.peer);
                // Don't do chunk/inv/stats setup until authenticated
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                const uint8_t* d   = ev.packet->data;
                size_t         len = ev.packet->dataLength;
                if (len == 0) { enet_packet_destroy(ev.packet); break; }

                uint8_t pid = d[0];

                // ── Auth request (must come first) ────────────────────────
                if (pid == (uint8_t)MPPacketID::AuthRequest) {
                    auto req = AuthRequestPacket::deserialize(d, len);
                    mpMgr.onAuthRequest(ev.peer, req, host.get());

                    // If authenticated, do normal connect setup
                    if (mpMgr.isAuthenticated(ev.peer)) {
                        chunks.addClient(ev.peer);
                        invMgr.onPlayerConnect(ev.peer, peerToUID(ev.peer));
                        statsMgr.onPlayerConnect(ev.peer);

                        float surfaceY = chunks.findSpawnY(0.f, 0.f);
                        float spawnY = surfaceY + Config::PLAYER_HEIGHT + 2.f;
                        positions[ev.peer] = {0.f, spawnY, 0.f};

                        chunks.updateClient(ev.peer, 0.f, spawnY, 0.f);
                        chunks.flushReady(host.get());

                        SpawnPositionPacket sp{0.f, spawnY, 0.f};
                        Net::sendReliable(ev.peer, sp.serialize());

                        invMgr.sendInventoryState(ev.peer);
                        statsMgr.sendFullSync(ev.peer);
                        enet_host_flush(host.get());
                    }

                    enet_packet_destroy(ev.packet);
                    break;
                }

                // All other packets require authentication
                if (!mpMgr.isAuthenticated(ev.peer)) {
                    enet_packet_destroy(ev.packet);
                    break;
                }

                if (pid == (uint8_t)PacketID::PlayerMove) {
                    auto mv = PlayerMovePacket::deserialize(d, len);
                    glm::vec3 pos{mv.x, mv.y, mv.z};
                    positions[ev.peer] = pos;
                    chunks.updateClient(ev.peer, mv.x, mv.y, mv.z);
                    invMgr.onPlayerMove(ev.peer, pos);
                    mpMgr.onPlayerMove(ev.peer, mv.x, mv.y, mv.z, mv.yaw, mv.pitch);

                } else if (pid == (uint8_t)PacketID::RespawnRequest) {
                    float surfaceY = chunks.findSpawnY(0.f, 0.f);
                    float spawnY = surfaceY + Config::PLAYER_HEIGHT + 2.f;
                    positions[ev.peer] = {0.f, spawnY, 0.f};

                    chunks.resetClient(ev.peer);
                    chunks.updateClient(ev.peer, 0.f, spawnY, 0.f);

                    SpawnPositionPacket sp{0.f, spawnY, 0.f};
                    Net::sendReliable(ev.peer, sp.serialize());
                    invMgr.sendInventoryState(ev.peer);
                    statsMgr.respawn(ev.peer);
                    enet_host_flush(host.get());

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
                Log::info("Peer disconnected");
                mpMgr.onPeerDisconnect(ev.peer, host.get());
                chunks.removeClient(ev.peer);
                invMgr.onPlayerDisconnect(ev.peer);
                statsMgr.onPlayerDisconnect(ev.peer);
                positions.erase(ev.peer);
                break;

            default:
                break;
            }
        }

        statsMgr.update(dt);

        statsFlushAccum += dt;
        if (statsFlushAccum >= 0.1f) {
            statsFlushAccum = 0.f;
            statsMgr.flushDirty();
            enet_host_flush(host.get());
        }

        // Broadcast player positions at ~20Hz
        possBroadcastAccum += dt;
        if (possBroadcastAccum >= 0.05f) {
            possBroadcastAccum = 0.f;
            mpMgr.broadcastPositions(host.get());
            enet_host_flush(host.get());
        }

        chunks.flushReady(host.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Net::deinit();
    Log::shutdown();
}
