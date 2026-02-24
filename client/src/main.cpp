#include "asset_path.h"
#include "camera.h"
#include "config.h"
#include "day_night.h"
#include "input.h"
#include "log.h"
#include "mesh_builder.h"
#include "net_common.h"
#include "packets.h"
#include "player.h"
#include "vk_context.h"
#include "window.h"

#include <chrono>
#include <enet/enet.h>
#include <entt/entt.hpp>

int main(int argc, char** argv) {
    AssetPath::init(argv[0]);
    Log::init("aetheris_client.log");
    Log::installCrashHandlers();
    Log::info("Client starting");

    Window   window(1280, 720, "Aetheris");
    VkContext ctx = vk_init(window.handle());

    Input            input(window.handle());
    Camera           camera;
    entt::registry   reg;
    PlayerController player(reg, camera);
    DayNight         dayNight;

    // One worker thread for mesh building on a 2-core Chromebook:
    // main thread stays free for render + input, worker does deserialize+march.
    MeshBuilder meshBuilder(1);

    Net::init();
    Net::Host host;

    ENetAddress addr{};
    enet_address_set_host(&addr, "127.0.0.1");
    addr.port = Config::SERVER_PORT;

    ENetPeer* server = enet_host_connect(host.get(), &addr, 2, 0);
    if (!server) { Log::err("enet_host_connect failed"); return 1; }

    {
        ENetEvent ev;
        if (enet_host_service(host.get(), &ev, 5000) > 0 &&
            ev.type == ENET_EVENT_TYPE_CONNECT) {
            Log::info("Connected to server");
        } else {
            Log::err("Connection failed");
            return 1;
        }
    }

    using Clock = std::chrono::steady_clock;
    auto  prev     = Clock::now();
    float netAccum = 0.f;

    std::vector<ChunkMesh> readyMeshes;

    while (!window.shouldClose()) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - prev).count();
        prev = now;
        if (dt > 0.05f) dt = 0.05f;

        input.beginFrame();

        // ── Receive packets (fast — no mesh work here) ────────────────────────
        ENetEvent ev;
        while (enet_host_service(host.get(), &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                const uint8_t* d   = ev.packet->data;
                size_t         len = ev.packet->dataLength;

                if (len > 0 && d[0] == (uint8_t)PacketID::ChunkData) {
                    // Hand off to worker — returns immediately
                    meshBuilder.submit(d, len);
                }
                else if (len > 0 && d[0] == (uint8_t)PacketID::SpawnPosition) {
                    auto sp = SpawnPositionPacket::deserialize(d, len);
                    player.setSpawnPosition({sp.x, sp.y, sp.z});
                }

                enet_packet_destroy(ev.packet);
            }
        }

        // ── Poll finished meshes (up to 4 per frame to avoid stutter) ─────────
        // On a slow device even 4 mesh integrations per frame is conservative;
        // tune upward if chunks arrive slowly and you want faster pop-in.
        readyMeshes.clear();
        meshBuilder.poll(readyMeshes, 4);
        for (auto& mesh : readyMeshes) {
            player.addChunkMesh(mesh);
            vk_upload_chunk(ctx, mesh); // queued internally, flushed in vk_draw
        }

        // ── Update ────────────────────────────────────────────────────────────
        player.update(dt, input);
        dayNight.update(dt);

        // ── Respawn ───────────────────────────────────────────────────────────
        if (input.keyPressed(GLFW_KEY_R)) {
            Net::sendReliable(server, RespawnRequestPacket{}.serialize());
            enet_host_flush(host.get());
        }

        // ── Send position (20 Hz) ─────────────────────────────────────────────
        netAccum += dt;
        if (netAccum >= 0.05f) {
            netAccum = 0.f;
            glm::vec3        pos = player.position();
            PlayerMovePacket mv{pos.x, pos.y, pos.z, camera.yaw, camera.pitch};
            Net::sendReliable(server, mv.serialize());
            enet_host_flush(host.get());
        }

        // ── Render ────────────────────────────────────────────────────────────
        int w, h;
        window.getSize(w, h);
        float     aspect = (w > 0 && h > 0) ? (float)w / (float)h : 1.f;
        glm::mat4 vp     = camera.viewProj(aspect);
        vk_draw(ctx, vp, dayNight.sunIntensity(), dayNight.skyColor());
    }

    enet_peer_disconnect(server, 0);
    enet_host_flush(host.get());
    vk_destroy(ctx);
    Net::deinit();
    Log::info("Client shutdown");
    Log::shutdown();
}
