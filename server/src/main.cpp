#include "chat_manager.h"
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
#include "spell_charge_packets.h"
#include "spell_manager.h"
#include "spell_packets.h"
#include "stats_manager.h"
#include "tree_system.h"
#include <chrono>
#include <enet/enet.h>
#include <unordered_map>

static uint64_t peerToUID(ENetPeer *peer) { return (uint64_t)(uintptr_t)peer; }
static constexpr int TREE_TEMPLATE_COUNT = 6;

static bool hasGrimoireEquipped(const Inventory &inv) {
    const ItemStack &offhand = inv.offhandSlot();
    return offhand.id == ItemID::WpnGrimoire;
}

int main(int argc, char **argv) {
    Log::init("aetheris_server.log");
    Log::installCrashHandlers();
    Log::info("Server starting");

    float worldTime          = 0.25f;
    float timeBroadcastAccum = 0.f;

    Net::init();
    Net::Host host(Config::SERVER_PORT, 32);

    ChunkManager      chunks(1);
    InventoryManager  invMgr;
    StatsManager      statsMgr;
    MultiplayerManager mpMgr;
    SpellManager      spellMgr;

    initTreeLibrary((int64_t)Config::WORLD_SEED);
    TreeSystem treeSys(getTreeLibrary());

    ChatManager chatMgr(mpMgr, invMgr);

    std::unordered_map<ENetPeer *, glm::vec3> positions;

    for (int i = 1; i + 1 < argc; i++) {
        if (std::string(argv[i]) == "--auth-host")
            mpMgr.authHost = argv[++i];
        else if (std::string(argv[i]) == "--auth-port")
            mpMgr.authPort = std::atoi(argv[++i]);
    }
    Log::info("Auth server: " + mpMgr.authHost + ":" + std::to_string(mpMgr.authPort));
    Log::info(std::string("Listening on port ") + std::to_string(Config::SERVER_PORT));

    // ── Load spells ───────────────────────────────────────────────────────────
    spellMgr.loadSpellsFromDir("spells/");

    // ── Wire spell primitives ─────────────────────────────────────────────────
    spellMgr.registerNative("aoe_damage",
        [&]([[maybe_unused]] Aether::NativeArgs args,
             Aether::NativeNamedArgs named) -> Aether::Value {
            float radius   = named.count("radius") ? (float)named["radius"].asNumber() : 2.f;
            float fraction = named.count("damage") ? (float)named["damage"].asNumber() : 1.f;
            fraction = std::clamp(fraction, 0.f, 1.f);

            float *budget = spellMgr.getFiringBudget();
            if (!budget || *budget <= 0.f) return Aether::Value::null();

            float damage = *budget * fraction;
            *budget -= damage;

            const CastState *cs = spellMgr.getCurrentFiringState();
            if (!cs) return Aether::Value::null();
            glm::vec3 targetPos{cs->aimX, cs->aimY, cs->aimZ};

            ENetPeer *caster = spellMgr.getCurrentFiringPeer();
            for (auto &[p, pos] : positions) {
                if (p == caster) continue;
                if (glm::length(pos - targetPos) <= radius)
                    statsMgr.applyDamage(p, damage);
            }
            Log::info("aoe_damage: " + std::to_string(damage) +
                      " budget left: " + std::to_string(*budget));
            return Aether::Value::null();
        });

    spellMgr.registerNative("aoe_heal",
        [&]([[maybe_unused]] Aether::NativeArgs args,
             Aether::NativeNamedArgs named) -> Aether::Value {
            float fraction = named.count("amount") ? (float)named["amount"].asNumber() : 1.f;
            fraction = std::clamp(fraction, 0.f, 1.f);

            float *budget = spellMgr.getFiringBudget();
            if (!budget || *budget <= 0.f) return Aether::Value::null();

            float amount = *budget * fraction;
            *budget -= amount;

            const CastState *cs = spellMgr.getCurrentFiringState();
            if (!cs) return Aether::Value::null();
            glm::vec3 targetPos{cs->aimX, cs->aimY, cs->aimZ};

            ENetPeer *caster = spellMgr.getCurrentFiringPeer();
            for (auto &[p, pos] : positions) {
                if (p == caster) continue;
                if (glm::length(pos - targetPos) <= 4.f)
                    statsMgr.applyHeal(p, amount);
            }
            // Also heal caster
            statsMgr.applyHeal(caster, amount * 0.5f);
            Log::info("aoe_heal: " + std::to_string(amount) +
                      " budget left: " + std::to_string(*budget));
            return Aether::Value::null();
        });

    spellMgr.registerNative("apply_status",
        []([[maybe_unused]] Aether::NativeArgs args,
           [[maybe_unused]] Aether::NativeNamedArgs named) -> Aether::Value {
            return Aether::Value::null();
        });

    spellMgr.registerNative("projectile",
        [&]([[maybe_unused]] Aether::NativeArgs args,
             Aether::NativeNamedArgs named) -> Aether::Value {
            float fraction = named.count("damage") ? (float)named["damage"].asNumber() : 1.f;
            fraction = std::clamp(fraction, 0.f, 1.f);

            float *budget = spellMgr.getFiringBudget();
            if (!budget || *budget <= 0.f) return Aether::Value::null();

            float damage = *budget * fraction;
            *budget -= damage;

            Log::info("projectile: damage=" + std::to_string(damage) +
                      " budget left: " + std::to_string(*budget));
            return Aether::Value::null();
        });

    spellMgr.registerNative("get_aim",
        [&]([[maybe_unused]] Aether::NativeArgs args,
            [[maybe_unused]] Aether::NativeNamedArgs named) -> Aether::Value {
            const CastState *cs = spellMgr.getCurrentFiringState();
            if (!cs) return Aether::Value::null();
            return Aether::Value::vec3(cs->aimX, cs->aimY, cs->aimZ);
        });

    spellMgr.registerNative("get_caster_pos",
        [&]([[maybe_unused]] Aether::NativeArgs args,
            [[maybe_unused]] Aether::NativeNamedArgs named) -> Aether::Value {
            ENetPeer *caster = spellMgr.getCurrentFiringPeer();
            if (!caster) return Aether::Value::null();
            auto it = positions.find(caster);
            if (it == positions.end()) return Aether::Value::null();
            return Aether::Value::vec3(it->second.x, it->second.y, it->second.z);
        });

    spellMgr.registerNative("get_caster_health",
        [&]([[maybe_unused]] Aether::NativeArgs args,
            [[maybe_unused]] Aether::NativeNamedArgs named) -> Aether::Value {
            ENetPeer *caster = spellMgr.getCurrentFiringPeer();
            if (!caster) return Aether::Value::null();
            auto *stats = statsMgr.get(caster);
            if (!stats) return Aether::Value::null();
            return Aether::Value::number(stats->health);
        });

    spellMgr.registerNative("__log__",
        [](Aether::NativeArgs args,
           [[maybe_unused]] Aether::NativeNamedArgs named) -> Aether::Value {
            if (!args.empty()) Log::info("[AES] " + args[0].toString());
            return Aether::Value::null();
        });

    // ── Find default spawn ────────────────────────────────────────────────────
    glm::vec3 defaultSpawn = chunks.findSpawnPos();
    Log::info("Default spawn: " + std::to_string(defaultSpawn.x) + ", " +
              std::to_string(defaultSpawn.y) + ", " +
              std::to_string(defaultSpawn.z));

    using Clock = std::chrono::steady_clock;
    auto  lastTick           = Clock::now();
    float statsFlushAccum    = 0.f;
    float possBroadcastAccum = 0.f;
    float spellStateAccum    = 0.f;

    while (true) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - lastTick).count();
        lastTick  = now;
        if (dt > 0.1f) dt = 0.1f;

        treeSys.update(dt);

        worldTime += dt / Config::DAY_LENGTH_SECONDS;
        if (worldTime > 1.f) worldTime -= 1.f;

        timeBroadcastAccum += dt;
        if (timeBroadcastAccum >= 1.0f) {
            timeBroadcastAccum = 0.f;
            WorldTimePacket pkt{worldTime};
            mpMgr.broadcastToAll(pkt.serialize());
        }

        // ── Network events ────────────────────────────────────────────────────
        ENetEvent ev;
        while (enet_host_service(host.get(), &ev, 0) > 0) {
            switch (ev.type) {

            case ENET_EVENT_TYPE_CONNECT:
                Log::info("Peer connected (awaiting auth)");
                mpMgr.onPeerConnect(ev.peer);
                break;

            case ENET_EVENT_TYPE_RECEIVE: {
                const uint8_t *d   = ev.packet->data;
                size_t         len = ev.packet->dataLength;
                if (len == 0) {
                    enet_packet_destroy(ev.packet);
                    break;
                }
                uint8_t pid = d[0];

                // ── Auth ──────────────────────────────────────────────────
                if (pid == (uint8_t)MPPacketID::AuthRequest) {
                    auto req = AuthRequestPacket::deserialize(d, len);
                    mpMgr.onAuthRequest(ev.peer, req, host.get());
                    if (mpMgr.isAuthenticated(ev.peer)) {
                        chunks.addClient(ev.peer);
                        invMgr.onPlayerConnect(ev.peer, peerToUID(ev.peer));
                        statsMgr.onPlayerConnect(ev.peer);

                        glm::vec3 sp = defaultSpawn;
                        invMgr.loadPlayerPos(peerToUID(ev.peer), sp);
                        sp.y += 5.0f;
                        positions[ev.peer] = sp;

                        chunks.updateClient(ev.peer, sp.x, sp.y, sp.z);
                        chunks.flushReady(host.get());
                        Net::sendReliable(ev.peer,
                            SpawnPositionPacket{sp.x, sp.y, sp.z}.serialize());
                        invMgr.sendInventoryState(ev.peer);
                        statsMgr.sendFullSync(ev.peer);
                        enet_host_flush(host.get());
                    }

                } else if (!mpMgr.isAuthenticated(ev.peer)) {
                    // drop unauthenticated packets

                // ── Movement ──────────────────────────────────────────────
                } else if (pid == (uint8_t)PacketID::PlayerMove) {
                    auto mv = PlayerMovePacket::deserialize(d, len);
                    glm::vec3 pos{mv.x, mv.y, mv.z};
                    positions[ev.peer] = pos;
                    chunks.updateClient(ev.peer, mv.x, mv.y, mv.z);
                    invMgr.onPlayerMove(ev.peer, pos);
                    mpMgr.onPlayerMove(ev.peer, mv.x, mv.y, mv.z, mv.yaw, mv.pitch);
                    invMgr.savePlayerPos(peerToUID(ev.peer), pos);

                // ── Respawn ────────────────────────────────────────────────
                } else if (pid == (uint8_t)PacketID::RespawnRequest) {
                    spellMgr.cancelCast(ev.peer);
                    glm::vec3 sp = defaultSpawn;
                    positions[ev.peer] = sp;
                    chunks.resetClient(ev.peer);
                    chunks.updateClient(ev.peer, sp.x, sp.y, sp.z);
                    chunks.flushReady(host.get());
                    Net::sendReliable(ev.peer,
                        SpawnPositionPacket{sp.x, sp.y, sp.z}.serialize());
                    statsMgr.respawn(ev.peer);
                    enet_host_flush(host.get());

                // ── Inventory ─────────────────────────────────────────────
                } else if (pid == (uint8_t)InvPacketID::ChestOpenReq) {
                    invMgr.onChestOpenReq(ev.peer,
                        ChestOpenReqPacket::deserialize(d, len));
                    enet_host_flush(host.get());

                } else if (pid == (uint8_t)InvPacketID::ChestCloseReq) {
                    invMgr.onChestCloseReq(ev.peer,
                        ChestCloseReqPacket::deserialize(d, len));

                } else if (pid == (uint8_t)InvPacketID::InventoryMoveReq) {
                    invMgr.onInventoryMoveReq(ev.peer,
                        InventoryMoveReqPacket::deserialize(d, len));
                    enet_host_flush(host.get());

                // ── Render distance ────────────────────────────────────────
                } else if (pid == (uint8_t)PacketID::RenderDist) {
                    auto pkt = RenderDistPacket::deserialize(d, len);
                    chunks.setClientRenderDist(ev.peer, pkt.xz, pkt.y);

                // ── Tree spawn ─────────────────────────────────────────────
                } else if (pid == (uint8_t)PacketID::SpawnTree) {
                    auto pkt = SpawnTreePacket::deserialize(d, len);
                    TreeSpawnPacket treePkt;
                    TreeSpawnPacket::Entry e;
                    e.wx = pkt.wx; e.wy = pkt.wy; e.wz = pkt.wz;
                    e.yaw       = 0.f;
                    e.scale     = 0.9f + (float)(rand() % 100) / 500.f;
                    e.templateIdx = (uint8_t)(rand() % TREE_TEMPLATE_COUNT);
                    treePkt.trees.push_back(e);
                    Net::sendReliable(ev.peer, treePkt.serialize());

                // ── World time override ────────────────────────────────────
                } else if (pid == (uint8_t)PacketID::WorldTime) {
                    auto pkt = WorldTimePacket::deserialize(d, len);
                    worldTime = pkt.time;
                    mpMgr.broadcastToAll(pkt.serialize());

                // ── Spell charge — begin ───────────────────────────────────
                } else if (pid == (uint8_t)SpellChargePacketID::Begin) {
                    auto pkt   = SpellChargeBeginPacket::deserialize(d, len);
                    auto *stats = statsMgr.get(ev.peer);
                    auto *inv   = invMgr.getPlayerInvPublic(ev.peer);
                    if (stats && inv && !stats->dead) {
                        bool ok = spellMgr.beginCharge(ev.peer, pkt.spellName,
                            pkt.aimX, pkt.aimY, pkt.aimZ, pkt.targetId, *stats, *inv);
                        if (!ok) {
                            SpellCastStatePacket sp{0, 0, 0, 0, 0};
                            Net::sendReliable(ev.peer, sp.serialize());
                        }
                    }

                // ── Spell charge — aim update ──────────────────────────────
                } else if (pid == (uint8_t)SpellChargePacketID::Tick) {
                    auto pkt = SpellChargeTickPacket::deserialize(d, len);
                    auto *cs = const_cast<CastState *>(spellMgr.getCastState(ev.peer));
                    if (cs) {
                        cs->aimX = pkt.aimX;
                        cs->aimY = pkt.aimY;
                        cs->aimZ = pkt.aimZ;
                    }

                // ── Spell charge — commit ──────────────────────────────────
                } else if (pid == (uint8_t)SpellChargePacketID::Commit) {
                    auto *stats = statsMgr.get(ev.peer);
                    if (stats) {
                        bool ok = spellMgr.commitCast(ev.peer, *stats);
                        if (ok) {
                            auto *cs = spellMgr.getCastState(ev.peer);
                            if (cs) {
                                SpellCastStatePacket sp{2, cs->manaCommitted,
                                    cs->castTimeTotal, 0.f, cs->interruptDC};
                                Net::sendReliable(ev.peer, sp.serialize());
                            }
                        }
                    }

                // ── Spell charge — cancel ──────────────────────────────────
                } else if (pid == (uint8_t)SpellChargePacketID::Cancel) {
                    auto *stats = statsMgr.get(ev.peer);
                    auto *cs    = spellMgr.getCastState(ev.peer);
                    if (cs && cs->isCharging() && stats) {
                        stats->mana += cs->manaCommitted;
                        stats->clamp();
                        statsMgr.markDirty(ev.peer);
                    }
                    spellMgr.cancelCast(ev.peer);
                    SpellCastStatePacket sp{0, 0, 0, 0, 0};
                    Net::sendReliable(ev.peer, sp.serialize());

                // ── Legacy direct cast ─────────────────────────────────────
                } else if (pid == (uint8_t)SpellPacketID::SpellCastReq) {
                    auto pkt = SpellCastReqPacket::deserialize(d, len);
                    auto *inv = invMgr.getPlayerInvPublic(ev.peer);
                    if (!inv || !hasGrimoireEquipped(*inv)) {
                        SpellCastFailPacket fail{"no_grimoire"};
                        Net::sendReliable(ev.peer, fail.serialize());
                    }

                // ── Chat ───────────────────────────────────────────────────
                } else if (pid == (uint8_t)ChatPacketID::ChatMessage) {
                    chatMgr.onMessage(ev.peer,
                        ChatMessagePacket::deserialize(d, len), host.get());
                    enet_host_flush(host.get());

                // ── Spell compile ──────────────────────────────────────────
                } else if (pid == (uint8_t)SpellBookPacketID::CompileReq) {
                    auto pkt = SpellCompileReqPacket::deserialize(d, len);
                    SpellCompileAckPacket ack;
                    ack.spellName = pkt.spellName;
                    bool ok = spellMgr.loadSpellSource(pkt.spellName, pkt.source);
                    ack.success = ok ? 1 : 0;
                    if (ok) {
                        auto meta    = spellMgr.getSpellMeta(pkt.spellName);
                        ack.baseMana = meta.baseMana;
                        ack.castTime = meta.castTime;
                    } else {
                        ack.error = "Compile failed — check syntax";
                    }
                    Net::sendReliable(ev.peer, ack.serialize());
                    enet_host_flush(host.get());

                // ── Spell loadout (no-op server side) ─────────────────────
                } else if (pid == (uint8_t)SpellBookPacketID::LoadoutSet) {
                    // spells already loaded by name at cast time

                // ── Spell delete (no-op server side for now) ───────────────
                } else if (pid == (uint8_t)SpellBookPacketID::DeleteReq) {
                    // spells stay loaded in VM until server restarts
                }

                enet_packet_destroy(ev.packet);
                break;
            } // end ENET_EVENT_TYPE_RECEIVE

            case ENET_EVENT_TYPE_DISCONNECT:
                Log::info("Peer disconnected");
                mpMgr.onPeerDisconnect(ev.peer, host.get());
                chunks.removeClient(ev.peer);
                invMgr.onPlayerDisconnect(ev.peer);
                statsMgr.onPlayerDisconnect(ev.peer);
                spellMgr.onPlayerRemoved(ev.peer);
                positions.erase(ev.peer);
                break;

            default:
                break;

            } // end switch
        } // end while enet_host_service

        // ── Stats update ──────────────────────────────────────────────────────
        statsMgr.update(dt);
        statsFlushAccum += dt;
        if (statsFlushAccum >= 0.1f) {
            statsFlushAccum = 0.f;
            statsMgr.flushDirty();
            enet_host_flush(host.get());
        }

        // ── Position broadcast ────────────────────────────────────────────────
        possBroadcastAccum += dt;
        if (possBroadcastAccum >= 0.05f) {
            possBroadcastAccum = 0.f;
            mpMgr.broadcastPositions(host.get());
            enet_host_flush(host.get());
        }

        // ── Spell system update ───────────────────────────────────────────────
        for (auto &[peer, pos] : positions) {
            auto *cs = spellMgr.getCastState(peer);
            if (!cs || !cs->isCharging()) continue;
            auto *stats = statsMgr.get(peer);
            if (!stats) continue;
            spellMgr.tickCharge(peer, dt, *stats);
            statsMgr.markDirty(peer);
        }

        auto firedSpells = spellMgr.update(dt);
        for (auto &fe : firedSpells) {
            auto posIt = positions.find(fe.peer);
            if (posIt == positions.end()) continue;

            glm::vec3 casterPos = posIt->second;
            glm::vec3 targetPos{fe.aimX, fe.aimY, fe.aimZ};

            Log::info("Spell fired: " + fe.spellName +
                      " potency=" + std::to_string(fe.potency) +
                      " mana=" + std::to_string(fe.manaSpent));

            SpellCastAckPacket ack;
            ack.spellName  = fe.spellName;
            ack.originX    = casterPos.x;
            ack.originY    = casterPos.y;
            ack.originZ    = casterPos.z;
            glm::vec3 dir{0, 0, 1};
            float dlen = glm::length(targetPos - casterPos);
            if (dlen > 0.001f) dir = (targetPos - casterPos) / dlen;
            ack.dirX         = dir.x;
            ack.dirY         = dir.y;
            ack.dirZ         = dir.z;
            ack.hasProjectile = 1;
            Net::sendReliable(fe.peer, ack.serialize());

            SpellCastStatePacket statePkt{0, 0, 0, 0, 0};
            Net::sendReliable(fe.peer, statePkt.serialize());
        }

        spellStateAccum += dt;
        if (spellStateAccum >= 0.1f) {
            spellStateAccum = 0.f;
            for (auto &[peer, pos] : positions) {
                auto *cs = spellMgr.getCastState(peer);
                if (!cs || cs->isIdle()) continue;
                SpellCastStatePacket pkt{(uint8_t)cs->phase, cs->manaCommitted,
                    cs->castTimeTotal, cs->castTimeElapsed, cs->interruptDC};
                Net::sendReliable(peer, pkt.serialize());
            }
            enet_host_flush(host.get());
        }

        chunks.flushReady(host.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // end game loop

    Net::deinit();
    Log::shutdown();
}
