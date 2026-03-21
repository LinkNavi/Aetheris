#include "chunk_manager.h"
#include "config.h"
#include "inv_packets.h"
#include "inventory_manager.h"
#include "log.h"
#include "mp_packets.h"
#include "multiplayer_manager.h"
#include "net_common.h"
#include "noise_gen.h"
#include "packets.h"
#include "player_stats.h"
#include "stats_manager.h"
#include "tree_system.h"
#include <chrono>
#include <enet/enet.h>
#include <unordered_map>

static uint64_t peerToUID(ENetPeer *peer) { return (uint64_t)(uintptr_t)peer; }
static constexpr int TREE_TEMPLATE_COUNT = 6;

int main(int argc, char **argv) {
  Log::init("aetheris_server.log");
  Log::installCrashHandlers();
  Log::info("Server starting");

  Net::init();
  Net::Host host(Config::SERVER_PORT, 32);

  ChunkManager chunks(1);

  InventoryManager invMgr;
  StatsManager statsMgr;
  MultiplayerManager mpMgr;
  initTreeLibrary((int64_t)Config::WORLD_SEED);
  TreeSystem treeSys(getTreeLibrary());

  for (int i = 1; i + 1 < argc; i++) {
    if (std::string(argv[i]) == "--auth-host") mpMgr.authHost = argv[++i];
    else if (std::string(argv[i]) == "--auth-port") mpMgr.authPort = std::atoi(argv[++i]);
  }
  Log::info("Auth server: " + mpMgr.authHost + ":" + std::to_string(mpMgr.authPort));
  Log::info(std::string("Listening on port ") + std::to_string(Config::SERVER_PORT));

  std::unordered_map<ENetPeer *, glm::vec3> positions;

  using Clock = std::chrono::steady_clock;
  auto lastTick = Clock::now();
  float statsFlushAccum = 0.f;
  float possBroadcastAccum = 0.f;

  while (true) {
    auto now = Clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
    if (dt > 0.1f) dt = 0.1f;

    treeSys.update(dt);

    ENetEvent ev;
    while (enet_host_service(host.get(), &ev, 0) > 0) {
      switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT:
        Log::info("Peer connected (awaiting auth)");
        mpMgr.onPeerConnect(ev.peer);
        break;

      case ENET_EVENT_TYPE_RECEIVE: {
        const uint8_t *d = ev.packet->data;
        size_t len = ev.packet->dataLength;
        if (len == 0) { enet_packet_destroy(ev.packet); break; }
        uint8_t pid = d[0];

        if (pid == (uint8_t)MPPacketID::AuthRequest) {
          auto req = AuthRequestPacket::deserialize(d, len);
          mpMgr.onAuthRequest(ev.peer, req, host.get());
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
          enet_packet_destroy(ev.packet); break;
        }

        if (!mpMgr.isAuthenticated(ev.peer)) { enet_packet_destroy(ev.packet); break; }

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
          chunks.flushReady(host.get());
          Net::sendReliable(ev.peer, SpawnPositionPacket{0.f, spawnY, 0.f}.serialize());
          statsMgr.respawn(ev.peer);
          enet_host_flush(host.get());
        } else if (pid == (uint8_t)InvPacketID::ChestOpenReq) {
          invMgr.onChestOpenReq(ev.peer, ChestOpenReqPacket::deserialize(d, len));
          enet_host_flush(host.get());
        } else if (pid == (uint8_t)InvPacketID::ChestCloseReq) {
          invMgr.onChestCloseReq(ev.peer, ChestCloseReqPacket::deserialize(d, len));
        } else if (pid == (uint8_t)InvPacketID::InventoryMoveReq) {
          invMgr.onInventoryMoveReq(ev.peer, InventoryMoveReqPacket::deserialize(d, len));
          enet_host_flush(host.get());
        } else if (pid == (uint8_t)PacketID::RenderDist) {
          auto pkt = RenderDistPacket::deserialize(d, len);
          chunks.setClientRenderDist(ev.peer, pkt.xz, pkt.y);
        } else if (pid == (uint8_t)PacketID::SpawnTree) {
          auto pkt = SpawnTreePacket::deserialize(d, len);
          TreeSpawnPacket treePkt;
          TreeSpawnPacket::Entry e;
          e.wx = pkt.wx; e.wy = pkt.wy; e.wz = pkt.wz; e.yaw = 0.f;
          e.scale = 0.9f + (float)(rand() % 100) / 500.f;
          e.templateIdx = (uint8_t)(rand() % TREE_TEMPLATE_COUNT);
          treePkt.trees.push_back(e);
          Net::sendReliable(ev.peer, treePkt.serialize());
        }
        enet_packet_destroy(ev.packet); break;
      }

      case ENET_EVENT_TYPE_DISCONNECT:
        Log::info("Peer disconnected");
        mpMgr.onPeerDisconnect(ev.peer, host.get());
        chunks.removeClient(ev.peer);
        invMgr.onPlayerDisconnect(ev.peer);
        statsMgr.onPlayerDisconnect(ev.peer);
        positions.erase(ev.peer);
        break;
      default: break;
      }
    }

    statsMgr.update(dt);
    statsFlushAccum += dt;
    if (statsFlushAccum >= 0.1f) {
      statsFlushAccum = 0.f; statsMgr.flushDirty(); enet_host_flush(host.get());
    }
    possBroadcastAccum += dt;
    if (possBroadcastAccum >= 0.05f) {
      possBroadcastAccum = 0.f; mpMgr.broadcastPositions(host.get()); enet_host_flush(host.get());
    }
    chunks.flushReady(host.get());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  Net::deinit(); Log::shutdown();
}
