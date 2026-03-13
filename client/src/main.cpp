#include "../include/combat_system.h"
#include "asset_path.h"
#include "camera.h"
#include "config.h"
#include "day_night.h"
#include "gltf_loader.h"
#include "hud.h"
#include "input.h"
#include "inv_packets.h"
#include "inventory.h"
#include "inventory_ui.h"
#include "log.h"
#include "mesh_builder.h"
#include "mp_packets.h"
#include "net_common.h"
#include "packets.h"
#include "player.h"
#include "player_stats.h"
#include "remote_players.h"
#include "view_model.h"
#include "vk_context.h"
#include "window.h"
#include <chrono>
#include <enet/enet.h>
#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "main_menu.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
int main(int argc, char **argv) {
  AssetPath::init(argv[0]);
  Log::init("aetheris_client.log");
  Log::installCrashHandlers();
  Log::info("Client starting");

  Window window(1280, 720, "Aetheris");
  VkContext ctx = vk_init(window.handle());
  vk_load_atlas(ctx, AssetPath::get("atlas.png").c_str());
  Input input(window.handle());
  Camera camera;
  entt::registry reg;
  PlayerController player(reg, camera);
  CombatSystem combat(reg);
  DayNight dayNight;
  MeshBuilder meshBuilder(1);
  InventoryUI invUI;
  HUD hud;
  ClientStats clientStats;
  MainMenu mainMenu;
  GameState gameState = GameState::MainMenu;
  bool cursorWasCaptured = false;
  ClientChestMirror chestMirror;
  RemotePlayerRenderer remotePlayers;

  // ── View model renderer ───────────────────────────────────────────────────
  ViewModelRenderer viewModel;
  viewModel.init(ctx.device.device, ctx.allocator, ctx.renderPass,
                 ctx.swapchain.extent,
                 AssetPath::get("viewmodel_vert.spv").c_str(),
                 AssetPath::get("viewmodel_frag.spv").c_str());

  viewModel.animEditor.open = false;

  VkDescriptorPool imguiPool;
  {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };
    VkDescriptorPoolCreateInfo pCI{};
    pCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pCI.maxSets = 1;
    pCI.poolSizeCount = 1;
    pCI.pPoolSizes = poolSizes;
    vkCreateDescriptorPool(ctx.device.device, &pCI, nullptr, &imguiPool);
  }

  // ── ImGui init ────────────────────────────────────────────────────────────
  {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window.handle(), true);
    ImGui_ImplVulkan_InitInfo imInfo{};
    imInfo.Instance = ctx.instance.instance;
    imInfo.PhysicalDevice = ctx.device.physical_device.physical_device;
    imInfo.Device = ctx.device.device;
    imInfo.QueueFamily = ctx.graphicsQueueFamily;
    imInfo.Queue = ctx.graphicsQueue;
    imInfo.DescriptorPool = ctx.imguiPool;
    imInfo.MinImageCount = 2;
    imInfo.ImageCount = (uint32_t)ctx.swapImages.size();
    imInfo.UseDynamicRendering = false;
    imInfo.RenderPass = ctx.renderPass;

    ImGui_ImplVulkan_Init(&imInfo);
  }

  // ── Load hand GLB ─────────────────────────────────────────────────────────
  {
    std::string glbPath = AssetPath::get("arm.glb");
    GltfModel model = loadGlb(glbPath.c_str());
    if (model.valid) {
      ViewModelTransform t;
      t.offset = {0.3900f, -0.2250f, -0.4050f};
      t.rotation = {-28.5f, 359.0f, -154.0f};
      t.scale = {0.21600f, 0.21300f, 0.21600f};
      int idx =
          viewModel.loadMesh(ctx.device.device, ctx.allocator, ctx.commandPool,
                             ctx.graphicsQueue, model, t);
      viewModel.setActiveMesh(idx);
    } else {
      Log::warn("No arm.glb found — viewmodel disabled.");
    }
  }

  // ── Load player model for remote players ──────────────────────────────────
  {
    std::string playerGlb = AssetPath::get("player.glb");
    if (remotePlayers.loadModel(ctx.device.device, ctx.allocator,
                                 ctx.commandPool, ctx.graphicsQueue,
                                 ctx.renderPass, ctx.swapchain.extent,
                                 playerGlb.c_str(),
                                 AssetPath::get("player_vert.spv").c_str(),
                                 AssetPath::get("player_frag.spv").c_str())) {
      Log::info("Player model loaded for multiplayer.");
    } else {
      Log::warn("No player.glb found — remote players will be invisible.");
    }
  }

  bool enemiesSpawned = false;
  bool authSent = false;

  Net::init();
  Net::Host host;
  ENetPeer *server = nullptr;

  using Clock = std::chrono::steady_clock;
  auto prev = Clock::now();
  float netAccum = 0.f;
  std::vector<ChunkMesh> readyMeshes;

  while (!window.shouldClose()) {
    auto now = Clock::now();
    float dt = std::chrono::duration<float>(now - prev).count();
    prev = now;
    if (dt > 0.05f) dt = 0.05f;
    input.beginFrame();

    if (gameState != GameState::InGame) {
      if (input.cursorCaptured()) input.captureCursor(false);
      int w, h; window.getSize(w, h);

      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      GameState next = mainMenu.draw(dt, w, h);

      if (next == GameState::Connecting) {
        if (mainMenu.pendingServerIP == "__QUIT__") break;

        const char* ip = mainMenu.pendingServerIP.c_str();
        int port = mainMenu.pendingServerPort;

        ENetAddress addr2{};
        if (enet_address_set_host(&addr2, ip) == 0) {
          addr2.port = (uint16_t)port;
          if (server) { enet_peer_disconnect_now(server, 0); server = nullptr; }
          server = enet_host_connect(host.get(), &addr2, 2, 0);
          if (server) {
            ENetEvent ev2;
            if (enet_host_service(host.get(), &ev2, 5000) > 0 &&
                ev2.type == ENET_EVENT_TYPE_CONNECT) {
              Log::info(std::string("Connected to ") + ip);

              // Send auth request immediately
              AuthRequestPacket authReq;
              authReq.username = mainMenu.pendingUsername;
              authReq.token = mainMenu.account().sessionToken;
              Net::sendReliable(server, authReq.serialize());
              enet_host_flush(host.get());
              authSent = true;

              gameState = GameState::InGame;
              input.captureCursor(true);

              // Clear remote players from previous session
              remotePlayers.players.clear();
              remotePlayers.localPlayerId = 0;

            } else {
              enet_peer_reset(server);
              server = nullptr;
            }
          }
        }
      } else {
        gameState = next;
      }

      ImGui::Render();
      vk_draw(ctx, glm::mat4(1.f), 0.f, {0.02f, 0.02f, 0.08f}, nullptr, glm::mat4(1.f));
      continue;
    }
    if (!server) continue;
    auto &cinv = reg.get<CInventory>(player.entity());

    // ── ] key — toggle viewmodel UI panels ───────────────────────────────
    if (input.keyDown(GLFW_KEY_RIGHT_BRACKET)) {
      viewModel.uiVisible = !viewModel.uiVisible;
      viewModel.animEditor.open = viewModel.uiVisible;
      if (viewModel.uiVisible && !cinv.open)
        input.captureCursor(false);
      else if (!viewModel.uiVisible && !cinv.open)
        input.captureCursor(true);
    }

    // ── Receive packets ───────────────────────────────────────────────────
    ENetEvent ev;
    while (enet_host_service(host.get(), &ev, 0) > 0) {
      if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
        const uint8_t *d = ev.packet->data;
        size_t len = ev.packet->dataLength;

        if (len > 0) {
          uint8_t pid = d[0];

          if (pid == (uint8_t)PacketID::ChunkData) {
            meshBuilder.submit(d, len);

          } else if (pid == (uint8_t)PacketID::SpawnPosition) {
            auto sp = SpawnPositionPacket::deserialize(d, len);
            player.setSpawnPosition({sp.x, sp.y, sp.z});
            enemiesSpawned = false;
            chestMirror.open = false;

          } else if (pid == (uint8_t)InvPacketID::InventoryState) {
            auto pkt = InventoryStatePacket::deserialize(d, len);
            invUI.applyState(cinv, pkt);

          } else if (pid == (uint8_t)InvPacketID::ChestState) {
            auto pkt = ChestStatePacket::deserialize(d, len);
            invUI.applyChestState(chestMirror, pkt);
            cinv.open = true;
            input.captureCursor(false);

          } else if (pid == (uint8_t)InvPacketID::InventoryMoveAck) {
            auto ack = InventoryMoveAckPacket::deserialize(d, len);
            invUI.applyAck(cinv, ack,
                           chestMirror.open ? &chestMirror : nullptr);

          } else if (pid == (uint8_t)InvPacketID::LootAvailable) {
            auto pkt = LootAvailablePacket::deserialize(d, len);
            Log::info("Loot available: corpse uid=" +
                      std::to_string(pkt.corpseUID));

          // ── Stats packets from server ────────────────────────────────
          } else if (pid == (uint8_t)StatsPacketID::StatsSync) {
            auto pkt = StatsSyncPacket::deserialize(d, len);
            clientStats.applySync(pkt);

          } else if (pid == (uint8_t)StatsPacketID::StatsDelta) {
            auto pkt = StatsDeltaPacket::deserialize(d, len);
            clientStats.applyDelta(pkt);

          // ── Multiplayer packets ──────────────────────────────────────
          } else if (pid == (uint8_t)MPPacketID::AuthResponse) {
            auto pkt = AuthResponsePacket::deserialize(d, len);
            if (pkt.accepted) {
              Log::info("Auth accepted: " + pkt.message + " (id=" + std::to_string(pkt.playerId) + ")");
              remotePlayers.localPlayerId = pkt.playerId;
            } else {
              Log::warn("Auth rejected: " + pkt.message);
              // Could kick back to menu, for now just log
            }

          } else if (pid == (uint8_t)MPPacketID::PlayerSpawn) {
            auto pkt = PlayerSpawnPacket::deserialize(d, len);
            remotePlayers.onSpawn(pkt);
            Log::info("Remote player spawned: " + pkt.username +
                      " (id=" + std::to_string(pkt.playerId) + ")");

          } else if (pid == (uint8_t)MPPacketID::PlayerDespawn) {
            auto pkt = PlayerDespawnPacket::deserialize(d, len);
            Log::info("Remote player left (id=" + std::to_string(pkt.playerId) + ")");
            remotePlayers.onDespawn(pkt.playerId);

          } else if (pid == (uint8_t)MPPacketID::PlayerPosSync) {
            auto pkt = PlayerPosSyncPacket::deserialize(d, len);
            remotePlayers.onPosSync(pkt);
          }
        }
        enet_packet_destroy(ev.packet);
      } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
        Log::info("Disconnected from server");
        server = nullptr;
        gameState = GameState::MainMenu;
        break;
      }
    }

    if (!server) continue;

    // ── Poll finished meshes ──────────────────────────────────────────────
    readyMeshes.clear();
    meshBuilder.poll(readyMeshes, 4);
    for (auto &mesh : readyMeshes) {
      player.addChunkMesh(mesh);
      vk_upload_chunk(ctx, mesh);
    }

    // ── Hotbar mode (Tab) + slot select (1-8) ────────────────────────────
    {
      bool tabPressed = input.keyDown(GLFW_KEY_TAB);
      int numKey = 0;
      static const int NUM_KEYS[] = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3,
                                     GLFW_KEY_4, GLFW_KEY_5, GLFW_KEY_6,
                                     GLFW_KEY_7, GLFW_KEY_8};
      for (int k = 0; k < 8; k++)
        if (input.keyDown(NUM_KEYS[k])) {
          numKey = k + 1;
          break;
        }

      invUI.handleInput(cinv, tabPressed, numKey);
    }

    // ── Inventory toggle (I) ──────────────────────────────────────────────
    if (input.keyDown(GLFW_KEY_I)) {
      cinv.open = !cinv.open;
      if (!cinv.open && chestMirror.open) {
        ChestCloseReqPacket req{chestMirror.uid};
        Net::sendReliable(server, req.serialize());
        chestMirror.open = false;
      }
      input.captureCursor(!cinv.open);
    }

    // ── Chest interaction (E) ─────────────────────────────────────────────
    if (input.keyDown(GLFW_KEY_E) && !chestMirror.open) {
      ChestOpenReqPacket req{1};
      Net::sendReliable(server, req.serialize());
      enet_host_flush(host.get());
    }

    bool uiOpen = cinv.open || chestMirror.open || viewModel.uiVisible;
    if (!uiOpen && !input.cursorCaptured())
      input.captureCursor(true);

    // ── Spawn test enemies ────────────────────────────────────────────────
    if (player.isSpawned() && !enemiesSpawned) {
      glm::vec3 base = player.position();
      combat.spawnEnemy(base + glm::vec3{5.f, 0.f, 0.f});
      combat.spawnEnemy(base + glm::vec3{-5.f, 0.f, 3.f});
      combat.spawnEnemy(base + glm::vec3{0.f, 0.f, -6.f});
      enemiesSpawned = true;
    }

    // ── Update ────────────────────────────────────────────────────────────
    bool suppressInput = uiOpen;
    if (suppressInput) {
      player.update(dt, input, nullptr);
    } else {
      bool lightAttack = input.keyDown(GLFW_KEY_F);
      bool heavyAttack = input.keyDown(GLFW_KEY_G);

      player.update(dt, input, &combat);

      if (lightAttack)
        viewModel.triggerLightAttack();
      if (heavyAttack)
        viewModel.triggerHeavyAttack();
    }

    combat.update(dt, player.entity());
    dayNight.update(dt);
    viewModel.update(dt);
    remotePlayers.update(dt);

    // ── Respawn ───────────────────────────────────────────────────────────
    if (input.keyPressed(GLFW_KEY_R)) {
      Net::sendReliable(server, RespawnRequestPacket{}.serialize());
      enet_host_flush(host.get());
    }

    // ── Send position (20 Hz) ─────────────────────────────────────────────
    netAccum += dt;
    if (netAccum >= 0.05f) {
      netAccum = 0.f;
      glm::vec3 pos = player.position();
      PlayerMovePacket mv{pos.x, pos.y, pos.z, camera.yaw, camera.pitch};
      Net::sendReliable(server, mv.serialize());
      enet_host_flush(host.get());
    }

    // ── Render ────────────────────────────────────────────────────────────
    int w, h;
    window.getSize(w, h);
    float aspect = (w > 0 && h > 0) ? (float)w / (float)h : 1.f;
    glm::mat4 vp = camera.viewProj(aspect);
    glm::mat4 proj = camera.proj(aspect);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Draw HUD (always visible)
    hud.draw(clientStats);

    // Draw nametags for remote players
    remotePlayers.drawNametags(vp, w, h);

    viewModel.drawDebugUI();
    invUI.draw(cinv, chestMirror.open ? &chestMirror : nullptr, server);

    ImGui::Render();
    vk_draw(ctx, vp, dayNight.sunIntensity(), dayNight.skyColor(), &viewModel,
            proj, &remotePlayers);
  }

  if (server) {
    enet_peer_disconnect(server, 0);
    enet_host_flush(host.get());
  }
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  vkDestroyDescriptorPool(ctx.device.device, imguiPool, nullptr);
  vkDeviceWaitIdle(ctx.device.device);
  remotePlayers.destroy(ctx.device.device, ctx.allocator);
  viewModel.destroy(ctx.device.device, ctx.allocator);
  vk_destroy(ctx);
  Net::deinit();
  Log::info("Client shutdown");
  Log::shutdown();
}
