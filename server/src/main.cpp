#include "chunk_manager.h"
#include "config.h"
#include "log.h"
#include "net_common.h"
#include "packets.h"
#include <enet/enet.h>
#include <unordered_map>

int main(int argc, char **argv) {
  Log::init("aetheris_server.log");
  Log::installCrashHandlers();
  Log::info("Server starting");

  Net::init();
  Net::Host host(Config::SERVER_PORT, 32);

  // Use 1 generation thread on low-end hardware (Chromebook has 2 cores total,
  // 1 for ENet/game loop, 1 for chunk gen). Increase on beefier servers.
  ChunkManager chunks(1);

  Log::info(std::string("Listening on port ") +
            std::to_string(Config::SERVER_PORT));

  std::unordered_map<ENetPeer *, glm::vec3> positions;

  while (true) {
    // ── Receive / handle events ───────────────────────────────────────────
    // enet_host_service timeout=0: non-blocking so we can flush ready chunks
    // on every iteration rather than blocking for up to 16ms.
    ENetEvent ev;
    while (enet_host_service(host.get(), &ev, 0) > 0) {
      switch (ev.type) {

      case ENET_EVENT_TYPE_CONNECT: {
        Log::info("Client connected");
        chunks.addClient(ev.peer);

        float spawnY = chunks.findSpawnY(0.f, 0.f);
        positions[ev.peer] = {0.f, spawnY, 0.f};

        // Schedule the two critical chunks first, synchronously
        // so they're guaranteed to be in the queue before SpawnPosition arrives
        chunks.updateClient(ev.peer, 0.f, spawnY, 0.f);

        // Don't send SpawnPosition yet — wait for client to request it
        // after chunks arrive. Instead send it last so chunks pipeline first.
        chunks.flushReady(host.get());

        SpawnPositionPacket sp{0.f, spawnY, 0.f};
        Net::sendReliable(ev.peer, sp.serialize());
        enet_host_flush(host.get());
        break;
      }

      case ENET_EVENT_TYPE_RECEIVE: {
        const uint8_t *d = ev.packet->data;
        size_t len = ev.packet->dataLength;

        if (len > 0 && d[0] == (uint8_t)PacketID::PlayerMove) {
          auto mv = PlayerMovePacket::deserialize(d, len);
          positions[ev.peer] = {mv.x, mv.y, mv.z};
          // Non-blocking — just schedules new chunks if player crossed boundary
          chunks.updateClient(ev.peer, mv.x, mv.y, mv.z);
        } else if (len > 0 && d[0] == (uint8_t)PacketID::RespawnRequest) {
          float spawnY = chunks.findSpawnY(0.f, 0.f);
          positions[ev.peer] = {0.f, spawnY, 0.f};

          chunks.resetClient(ev.peer);
          chunks.updateClient(ev.peer, 0.f, spawnY, 0.f);

          SpawnPositionPacket sp{0.f, spawnY, 0.f};
          Net::sendReliable(ev.peer, sp.serialize());
          enet_host_flush(host.get());
          Log::info(std::string("Respawn at y=") + std::to_string(spawnY));
        }

        enet_packet_destroy(ev.packet);
        break;
      }

      case ENET_EVENT_TYPE_DISCONNECT:
        Log::info("Client disconnected");
        chunks.removeClient(ev.peer);
        positions.erase(ev.peer);
        break;

      default:
        break;
      }
    }

    // ── Send any chunks that finished generating this tick ─────────────────
    // This is O(ready_count) and never blocks.
    chunks.flushReady(host.get());

    // ── Yield to OS briefly to avoid 100% CPU spin ────────────────────────
    // 1ms sleep — gives worker threads time to run on the same core.
    // On a 2-core machine this matters: without a yield the ENet thread
    // can starve the gen thread.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  Net::deinit();
  Log::shutdown();
}
