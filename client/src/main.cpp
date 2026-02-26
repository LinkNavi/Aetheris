#include "asset_path.h"
#include "camera.h"
#include "config.h"
#include "../include/combat_system.h"
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
    CombatSystem     combat(reg);
    DayNight         dayNight;

    MeshBuilder meshBuilder(1);

    // Spawn a couple of test enemies once the player spawns
    bool enemiesSpawned = false;

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

        // ── Receive packets ───────────────────────────────────────────────────
        ENetEvent ev;
        while (enet_host_service(host.get(), &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                const uint8_t* d   = ev.packet->data;
                size_t         len = ev.packet->dataLength;

                if (len > 0 && d[0] == (uint8_t)PacketID::ChunkData) {
                    meshBuilder.submit(d, len);
                }
                else if (len > 0 && d[0] == (uint8_t)PacketID::SpawnPosition) {
                    auto sp = SpawnPositionPacket::deserialize(d, len);
                    player.setSpawnPosition({sp.x, sp.y, sp.z});
                    enemiesSpawned = false; // reset so enemies respawn after respawn
                }

                enet_packet_destroy(ev.packet);
            }
        }

        // ── Poll finished meshes ──────────────────────────────────────────────
        readyMeshes.clear();
        meshBuilder.poll(readyMeshes, 4);
        for (auto& mesh : readyMeshes) {
            player.addChunkMesh(mesh);
            vk_upload_chunk(ctx, mesh);
        }

        // ── Spawn test enemies once we have a position ────────────────────────
        if (player.isSpawned() && !enemiesSpawned) {
            glm::vec3 base = player.position();
            combat.spawnEnemy(base + glm::vec3{ 5.f, 0.f,  0.f});
            combat.spawnEnemy(base + glm::vec3{-5.f, 0.f,  3.f});
            combat.spawnEnemy(base + glm::vec3{ 0.f, 0.f, -6.f});
            enemiesSpawned = true;
        }

        // ── Update ────────────────────────────────────────────────────────────
        player.update(dt, input, &combat);
        combat.update(dt, player.entity());
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