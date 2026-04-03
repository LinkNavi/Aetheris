#include "../include/combat_system.h"
#include "asset_path.h"
#include "camera.h"
#include "chat_packets.h"
#include "chat_ui.h"
#include "config.h"
#include "day_night.h"
#include "debug_menu.h"
#include "gltf_loader.h"
#include "hud.h"
#include "input.h"
#include "inv_packets.h"
#include "inventory.h"
#include "inventory_ui.h"
#include "keybinds_impl.h"
#include "log.h"
#include "main_menu.h"
#include "mesh_builder.h"
#include "mp_packets.h"
#include "net_common.h"
#include "noise_gen.h"
#include "packets.h"
#include "player.h"
#include "player_stats.h"
#include "remote_players.h"
#include "spell_charge_packets.h"
#include "spell_charge_ui.h"
#include "spell_editor.h"
#include "spell_packets.h"
#include "tree_renderer.h"
#include "view_model.h"
#include "vk_context.h"
#include "window.h"

#include <chrono>
#include <cstring>
#include <enet/enet.h>
#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

std::unordered_map<ChunkCoord, ChunkData, ChunkCoordHash> terrainCache;

int main(int /*argc*/, char **argv) {
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
    SpellChargeUI spellUI;
    HUD hud;
    ClientStats clientStats;
    MainMenu mainMenu;
    GameState gameState = GameState::MainMenu;
    ClientChestMirror chestMirror;
    RemotePlayerRenderer remotePlayers;
    ChatUI chat;
    SpellEditorUI spellEditor;
    float appTime = 0.f;

    ViewModelRenderer viewModel;
    viewModel.init(ctx.device.device, ctx.allocator, ctx.renderPass,
                   ctx.swapchain.extent,
                   AssetPath::get("viewmodel_vert.spv").c_str(),
                   AssetPath::get("viewmodel_frag.spv").c_str());
    viewModel.animEditor.open = false;

    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(ctx.device.physical_device.physical_device, &props);
        bool isIntelIntegrated = (props.vendorID == 0x8086) &&
            (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
        if (isIntelIntegrated) {
            Log::info("Low-end Intel GPU detected — enabling performance mode");
            mainMenu.settings().renderDistance = 2.f;
        }
    }

    VkDescriptorPool imguiPool;
    {
        VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
        VkDescriptorPoolCreateInfo pCI{};
        pCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pCI.maxSets       = 1;
        pCI.poolSizeCount = 1;
        pCI.pPoolSizes    = poolSizes;
        vkCreateDescriptorPool(ctx.device.device, &pCI, nullptr, &imguiPool);
    }

    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(window.handle(), true);
        ImGui_ImplVulkan_InitInfo imInfo{};
        imInfo.Instance       = ctx.instance.instance;
        imInfo.PhysicalDevice = ctx.device.physical_device.physical_device;
        imInfo.Device         = ctx.device.device;
        imInfo.QueueFamily    = ctx.graphicsQueueFamily;
        imInfo.Queue          = ctx.graphicsQueue;
        imInfo.DescriptorPool = ctx.imguiPool;
        imInfo.MinImageCount  = 2;
        imInfo.ImageCount     = (uint32_t)ctx.swapImages.size();
#if IMGUI_VERSION_NUM >= 19260
        imInfo.PipelineInfoMain.RenderPass    = ctx.renderPass;
        imInfo.PipelineInfoMain.MSAASamples   = VK_SAMPLE_COUNT_1_BIT;
#elif IMGUI_VERSION_NUM >= 18960
        imInfo.RenderPass   = ctx.renderPass;
        imInfo.MSAASamples  = VK_SAMPLE_COUNT_1_BIT;
#endif
        ImGui_ImplVulkan_Init(&imInfo);
    }

    // ── Load arm ──────────────────────────────────────────────────────────────
    {
        GltfModel model = loadGlb(AssetPath::get("arm.glb").c_str());
        if (model.valid) {
            ViewModelTransform t;
            t.offset   = {0.3900f, -0.2250f, -0.4050f};
            t.rotation = {-28.5f,  359.0f,  -154.0f};
            t.scale    = {0.21600f, 0.21300f, 0.21600f};
            viewModel.armMeshIdx = viewModel.loadMesh(
                ctx.device.device, ctx.allocator, ctx.commandPool,
                ctx.graphicsQueue, model, t);
        }
    }

    // ── Load item meshes ───────────────────────────────────────────────────────
    {
        GltfModel bookModel = loadGlb(AssetPath::get("grimoire.glb").c_str());
        if (bookModel.valid) {
            int idx = viewModel.loadMesh(ctx.device.device, ctx.allocator,
                                         ctx.commandPool, ctx.graphicsQueue, bookModel, {});
            ViewModelTransform main{}, offhand{};
            offhand.offset   = {0.4300f, -0.2400f, -0.5950f};
            offhand.rotation = {-15.0f,   79.0f,    68.0f};
            offhand.scale    = {0.18000f,  0.18000f,  0.18000f};
            viewModel.registerItemMesh(ItemID::WpnGrimoire, idx, main, offhand);
        }
    }

    {
        std::string playerGlb = AssetPath::get("player.glb");
        remotePlayers.loadModel(ctx.device.device, ctx.allocator, ctx.commandPool,
                                ctx.graphicsQueue, ctx.renderPass, ctx.swapchain.extent,
                                playerGlb.c_str(),
                                AssetPath::get("player_vert.spv").c_str(),
                                AssetPath::get("player_frag.spv").c_str());
    }

    TreeRenderer treeRenderer;
    treeRenderer.init(ctx.device.device, ctx.allocator, ctx.commandPool,
                      ctx.graphicsQueue, ctx.renderPass, ctx.swapchain.extent,
                      AssetPath::get("tree_trunk_vert.spv").c_str(),
                      AssetPath::get("tree_trunk_frag.spv").c_str(),
                      AssetPath::get("tree_leaf_vert.spv").c_str(),
                      AssetPath::get("tree_leaf_frag.spv").c_str());

    DebugMenu debugMenu;
    bool enemiesSpawned = false;

    Net::init();
    Net::Host host;
    ENetPeer *server = nullptr;

    using Clock = std::chrono::steady_clock;
    auto  prev     = Clock::now();
    float netAccum = 0.f;
    std::vector<ChunkMesh> readyMeshes;
    Keybinds &kb = mainMenu.keybinds();

    while (!window.shouldClose()) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - prev).count();
        prev = now;
        if (dt > 0.05f) dt = 0.05f;
        appTime += dt;

        input.beginFrame();

        int w, h;
        window.getSize(w, h);
        static int prevW = w, prevH = h;
        if (w != prevW || h != prevH) {
            prevW = w; prevH = h;
            if (w > 0 && h > 0) vk_resize(ctx, window.handle());
        }

        // ── Main menu ─────────────────────────────────────────────────────────
        if (gameState != GameState::InGame) {
            if (input.cursorCaptured()) input.captureCursor(false);
            int w2, h2;
            window.getSize(w2, h2);
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            GameState next = mainMenu.draw(dt, w2, h2, &input);

            float prevLeafDensity = treeRenderer.leafDensity;
            float newLeafDensity  = mainMenu.settings().leafDensity;
            if (newLeafDensity != prevLeafDensity) {
                treeRenderer.leafDensity = newLeafDensity;
                treeRenderer.rebuildMeshes();
            }

            if (next == GameState::Connecting) {
                if (mainMenu.pendingServerIP == "__QUIT__") break;
                const char *ip   = mainMenu.pendingServerIP.c_str();
                int          port = mainMenu.pendingServerPort;
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
                            AuthRequestPacket authReq;
                            authReq.username = mainMenu.pendingUsername;
                            authReq.token    = mainMenu.account().sessionToken;
                            Net::sendReliable(server, authReq.serialize());
                            enet_host_flush(host.get());
                            RenderDistPacket rd;
                            rd.xz = (uint8_t)std::clamp((int)mainMenu.settings().renderDistance, 1, 255);
                            rd.y  = 4;
                            Net::sendReliable(server, rd.serialize());
                            enet_host_flush(host.get());
                            gameState = GameState::InGame;
                            mainMenu.keybinds().load();
                            input.captureCursor(true);
                            remotePlayers.players.clear();
                            remotePlayers.localPlayerId = 0;
                            chat.pushSystem("Connected to " + std::string(ip));
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
            vk_draw(ctx, glm::mat4(1.f), glm::mat4(1.f), nullptr, 0.f,
                    {0.02f, 0.02f, 0.08f}, 2, glm::vec3(0.f), nullptr,
                    glm::mat4(1.f), nullptr, nullptr);
            continue;
        }

        if (!server) continue;

        auto &cinv = reg.get<CInventory>(player.entity());

        // ── Chat key handling ─────────────────────────────────────────────────
        if (input.keyDown(GLFW_KEY_ENTER) || input.keyDown(GLFW_KEY_KP_ENTER))
            chat.onKeyDown(GLFW_KEY_ENTER);
        if (input.keyDown(GLFW_KEY_ESCAPE) && chat.isOpen())
            chat.onKeyDown(GLFW_KEY_ESCAPE);

        // ── View model debug toggle ───────────────────────────────────────────
        if (!chat.isOpen() && !spellEditor.open &&
            input.keyDown(GLFW_KEY_RIGHT_BRACKET)) {
            viewModel.uiVisible   = !viewModel.uiVisible;
            viewModel.animEditor.open = viewModel.uiVisible;
            input.captureCursor(!viewModel.uiVisible && !cinv.open);
        }

        // ── Receive packets ───────────────────────────────────────────────────
        ENetEvent ev;
        while (enet_host_service(host.get(), &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                const uint8_t *d   = ev.packet->data;
                size_t         len = ev.packet->dataLength;
                if (len > 0) {
                    uint8_t pid = d[0];
                    if (pid == (uint8_t)PacketID::ChunkData) {
                        meshBuilder.submit(d, len);
                    } else if (pid == (uint8_t)PacketID::SpawnPosition) {
                        auto sp = SpawnPositionPacket::deserialize(d, len);
                        player.setSpawnPosition({sp.x, sp.y, sp.z});
                        enemiesSpawned  = false;
                        chestMirror.open = false;
                    } else if (pid == (uint8_t)InvPacketID::InventoryState) {
                        invUI.applyState(cinv, InventoryStatePacket::deserialize(d, len));
                    } else if (pid == (uint8_t)InvPacketID::ChestState) {
                        auto pkt = ChestStatePacket::deserialize(d, len);
                        invUI.applyChestState(chestMirror, pkt);
                        cinv.open = true;
                        input.captureCursor(false);
                    } else if (pid == (uint8_t)InvPacketID::InventoryMoveAck) {
                        invUI.applyAck(cinv, InventoryMoveAckPacket::deserialize(d, len),
                                       chestMirror.open ? &chestMirror : nullptr);
                    } else if (pid == (uint8_t)StatsPacketID::StatsSync) {
                        clientStats.applySync(StatsSyncPacket::deserialize(d, len));
                    } else if (pid == (uint8_t)StatsPacketID::StatsDelta) {
                        clientStats.applyDelta(StatsDeltaPacket::deserialize(d, len));
                    } else if (pid == (uint8_t)MPPacketID::AuthResponse) {
                        auto pkt = AuthResponsePacket::deserialize(d, len);
                        if (pkt.accepted) remotePlayers.localPlayerId = pkt.playerId;
                    } else if (pid == (uint8_t)MPPacketID::PlayerSpawn) {
                        remotePlayers.onSpawn(PlayerSpawnPacket::deserialize(d, len));
                    } else if (pid == (uint8_t)MPPacketID::PlayerDespawn) {
                        remotePlayers.onDespawn(PlayerDespawnPacket::deserialize(d, len).playerId);
                    } else if (pid == (uint8_t)MPPacketID::PlayerPosSync) {
                        remotePlayers.onPosSync(PlayerPosSyncPacket::deserialize(d, len));
                    } else if (pid == (uint8_t)PacketID::TreeSpawn) {
                        auto pkt = TreeSpawnPacket::deserialize(d, len);
                        for (auto &t : pkt.trees)
                            treeRenderer.addTree({t.wx, t.wy, t.wz}, t.yaw, t.scale, t.templateIdx);
                    } else if (pid == (uint8_t)PacketID::WorldTime) {
                        auto pkt = WorldTimePacket::deserialize(d, len);
                        if (!debugMenu.timeOverride) dayNight.time = pkt.time;
                    } else if (pid == (uint8_t)ChatPacketID::ChatBroadcast) {
                        auto pkt = ChatBroadcastPacket::deserialize(d, len);
                        chat.pushMessage(pkt.username, pkt.text);
                    } else if (pid == (uint8_t)SpellChargePacketID::State) {
                        spellUI.applyState(SpellCastStatePacket::deserialize(d, len));
                    } else if (pid == (uint8_t)SpellPacketID::SpellCastAck) {
                        // TODO: VFX
                    } else if (pid == (uint8_t)SpellBookPacketID::CompileAck) {
                        spellEditor.onCompileAck(cinv, SpellCompileAckPacket::deserialize(d, len));
                    }
                }
                enet_packet_destroy(ev.packet);
            } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                Log::info("Disconnected from server");
                chat.pushSystem("Disconnected from server.");
                server    = nullptr;
                gameState = GameState::MainMenu;
                treeRenderer.clearTrees();
                terrainCache.clear();
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
            terrainCache[mesh.coord] = generateChunk(mesh.coord);
        }
        if (terrainCache.size() > 512) terrainCache.clear();

        // ── Input handling (suppressed when chat or spell editor open) ─────────
        ImGuiIO& io = ImGui::GetIO();
if (!io.WantCaptureKeyboard && !chat.isOpen() && !spellEditor.open) {
            bool tabPressed = input.keyDown(GLFW_KEY_TAB);
            int  numKey     = 0;
            static const int NUM_KEYS[] = {
                GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
                GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8};
            for (int k = 0; k < 8; k++)
                if (input.keyDown(NUM_KEYS[k])) { numKey = k + 1; break; }
            invUI.handleInput(cinv, tabPressed, numKey);

            // Spell editor toggle — requires grimoire
            if (kb.isDown(Action::SpellEditor, input)) {
                const ItemStack &offhand = cinv.inv.offhandSlot();
                if (offhand.id == ItemID::WpnGrimoire) {
                    spellEditor.open = true;
                    input.captureCursor(false);
                } else {
                    chat.pushSystem("Requires a Grimoire in your offhand.");
                }
            }

            // Inventory
            if (kb.isDown(Action::Inventory, input)) {
                cinv.open = !cinv.open;
                if (!cinv.open && chestMirror.open) {
                    Net::sendReliable(server, ChestCloseReqPacket{chestMirror.uid}.serialize());
                    chestMirror.open = false;
                }
                input.captureCursor(!cinv.open);
            }

            // Interact
            if (kb.isDown(Action::Interact, input) && !chestMirror.open) {
                Net::sendReliable(server, ChestOpenReqPacket{1}.serialize());
                enet_host_flush(host.get());
            }

            // Debug menu
            if (kb.isDown(Action::DebugMenu, input)) {
                debugMenu.toggle();
                if (debugMenu.visible)
                    input.captureCursor(false);
                else if (!cinv.open && !chestMirror.open && !viewModel.uiVisible)
                    input.captureCursor(true);
            }

            // Respawn
            if (kb.isDown(Action::Respawn, input)) {
                Net::sendReliable(server, RespawnRequestPacket{}.serialize());
                enet_host_flush(host.get());
            }
        }

        // Spell editor close (always checked so ESC can close it)
        if (spellEditor.open && input.keyDown(GLFW_KEY_ESCAPE)) {
            spellEditor.open = false;
            input.captureCursor(true);
        }

        spellEditor.update(dt);

        // ── UI / cursor state ─────────────────────────────────────────────────
        bool uiOpen  = cinv.open || chestMirror.open || viewModel.uiVisible ||
                       debugMenu.visible || chat.isOpen() || spellEditor.open;
        bool canCast = !uiOpen && !clientStats.dead;

        if (uiOpen && input.cursorCaptured())   input.captureCursor(false);
        else if (!uiOpen && !input.cursorCaptured()) input.captureCursor(true);

        // ── Spell casting ─────────────────────────────────────────────────────
        spellUI.localMana = clientStats.mana;
        spellUI.maxMana   = clientStats.manaMax;
        spellUI.update(dt);

        if (canCast) {
            int activeSpellSlot = -1, heldSpellSlot = -1;
            for (int slot = 0; slot < SPELL_SLOTS; slot++) {
                Action a = (Action)((int)Action::SpellSlot1 + slot);
                if (kb.isDown(a, input) && activeSpellSlot < 0) activeSpellSlot = slot;
                if (kb.isHeld(a, input) && heldSpellSlot  < 0) heldSpellSlot  = slot;
            }

            bool spellReleased = (heldSpellSlot < 0 && spellUI.phase == 1);

            if (activeSpellSlot >= 0 && spellUI.phase == 0) {
                SpellEntry *spell = cinv.inv.getActiveSpell(activeSpellSlot);
                std::string spellName = (spell && !spell->empty()) ? spell->name : "fireball";
                glm::vec3 aimPos = camera.position + camera.forward() * 20.f;
                spellUI.onKeyDown(
                    kb.get((Action)((int)Action::SpellSlot1 + activeSpellSlot)),
                    spellName, aimPos.x, aimPos.y, aimPos.z, server);
            }
            if (spellReleased) spellUI.onKeyUp(server);
            if (input.keyDown(GLFW_KEY_ESCAPE) && spellUI.phase != 0)
                spellUI.onCancel(server);
        }

        // ── Spawn enemies once ────────────────────────────────────────────────
        if (player.isSpawned() && !enemiesSpawned) {
            glm::vec3 base = player.position();
            combat.spawnEnemy(base + glm::vec3{ 5.f, 0.f,  0.f});
            combat.spawnEnemy(base + glm::vec3{-5.f, 0.f,  3.f});
            combat.spawnEnemy(base + glm::vec3{ 0.f, 0.f, -6.f});
            enemiesSpawned = true;
        }

        viewModel.syncEquipped(cinv.inv.weaponSlot().id, cinv.inv.offhandSlot().id);

        // ── Update ────────────────────────────────────────────────────────────
      if (uiOpen || io.WantCaptureKeyboard) {
    player.update(dt, input, nullptr);
} else {
            bool lightAttack = kb.isDown(Action::LightAttack, input);
            bool heavyAttack = kb.isDown(Action::HeavyAttack, input);
            player.update(dt, input, &combat);
            if (lightAttack) viewModel.triggerLightAttack();
            if (heavyAttack) viewModel.triggerHeavyAttack();
        }

        if (dt < 0.040f) treeRenderer.update(dt);
        else             treeRenderer.update(0.016f);

        combat.update(dt, player.entity());
        dayNight.update(dt);
        viewModel.update(dt);
        remotePlayers.update(dt);
        ctx.skyGodRay.update(dt);

        // ── Network position send ─────────────────────────────────────────────
        netAccum += dt;
        if (netAccum >= 0.05f) {
            netAccum = 0.f;
            glm::vec3 pos = player.position();
            Net::sendReliable(server,
                PlayerMovePacket{pos.x, pos.y, pos.z, camera.yaw, camera.pitch}.serialize());
            enet_host_flush(host.get());
        }

        // ── Render ────────────────────────────────────────────────────────────
        window.getSize(w, h);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        hud.draw(clientStats);
        chat.draw(dt, appTime, server);

        if (!spellEditor.open) {
            invUI.draw(cinv, chestMirror.open ? &chestMirror : nullptr, server);
            viewModel.drawDebugUI();
            debugMenu.draw(player.position(), server, dayNight);
        }

        spellUI.draw();
        spellEditor.draw(cinv, server);

        ImGui::Render();

        float     aspect = (w > 0 && h > 0) ? (float)w / (float)h : 1.f;
        glm::mat4 vp     = camera.viewProj(aspect);
        glm::mat4 proj   = camera.proj(aspect);
        int rdXZ = (int)std::clamp((int)mainMenu.settings().renderDistance, 1, 255);

        vk_draw(ctx, vp, camera.view(), &treeRenderer, dayNight.sunIntensity(),
                dayNight.skyColor(), rdXZ, camera.position, &viewModel, proj,
                &remotePlayers, &dayNight);
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    if (server) {
        enet_peer_disconnect_now(server, 0);
        enet_host_flush(host.get());
        ENetEvent ev2;
        uint32_t timeout = 2000;
        while (enet_host_service(host.get(), &ev2, timeout) > 0) {
            if (ev2.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev2.packet);
            else if (ev2.type == ENET_EVENT_TYPE_DISCONNECT) break;
            timeout = 100;
        }
        server = nullptr;
    }

    meshBuilder.cancelPending();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    treeRenderer.destroy(ctx.device.device, ctx.allocator);
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
