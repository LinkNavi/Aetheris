#pragma once
#include <enet/enet.h>
#include <unordered_map>
#include "player_stats.h"
#include "net_common.h"

class StatsManager {
public:
    void onPlayerConnect(ENetPeer* peer) {
        _stats[peer] = PlayerStats{};
        _dirty[peer] = true;
    }

    void onPlayerDisconnect(ENetPeer* peer) {
        _stats.erase(peer);
        _dirty.erase(peer);
    }

    PlayerStats* get(ENetPeer* peer) {
        auto it = _stats.find(peer);
        return it != _stats.end() ? &it->second : nullptr;
    }

    void markDirty(ENetPeer* peer) { _dirty[peer] = true; }

    // Call when player takes damage (after armour reduction)
    void applyDamage(ENetPeer* peer, float raw) {
        auto* s = get(peer);
        if (!s || s->dead) return;
        float reduced = raw * (1.f - s->armour / (s->armour + 100.f)); // diminishing returns
        s->health -= reduced;
        s->clamp();
        _dirty[peer] = true;
    }

    void applyHeal(ENetPeer* peer, float amount) {
        auto* s = get(peer);
        if (!s || s->dead) return;
        s->health += amount;
        s->clamp();
        _dirty[peer] = true;
    }

    void spendMana(ENetPeer* peer, float amount) {
        auto* s = get(peer);
        if (!s) return;
        s->mana -= amount;
        s->clamp();
        _dirty[peer] = true;
    }

    void respawn(ENetPeer* peer) {
        auto* s = get(peer);
        if (!s) return;
        s->reset();
        _dirty[peer] = true;
        // Send full sync on respawn
        sendFullSync(peer);
    }

    // Called every server tick with dt
    void update(float dt) {
        for (auto& [peer, stats] : _stats) {
            if (stats.dead) continue;
            bool changed = false;

            // Stamina regen
            if (stats.stamina < stats.staminaMax) {
                stats.stamina += 15.f * dt;
                if (stats.stamina > stats.staminaMax) stats.stamina = stats.staminaMax;
                changed = true;
            }

            // Mana regen
            if (stats.mana < stats.manaMax) {
                stats.mana += 5.f * dt;
                if (stats.mana > stats.manaMax) stats.mana = stats.manaMax;
                changed = true;
            }

            if (changed) _dirty[peer] = true;
        }
    }

    // Send delta packets for dirty stats (call at ~10Hz from server tick)
    void flushDirty() {
        for (auto& [peer, dirty] : _dirty) {
            if (!dirty) continue;
            auto* s = get(peer);
            if (!s) continue;
            auto pkt = StatsDeltaPacket::from(*s);
            Net::sendReliable(peer, pkt.serialize());
            dirty = false;
        }
    }

    void sendFullSync(ENetPeer* peer) {
        auto* s = get(peer);
        if (!s) return;
        auto pkt = StatsSyncPacket::from(*s);
        Net::sendReliable(peer, pkt.serialize());
        _dirty[peer] = false;
    }

private:
    std::unordered_map<ENetPeer*, PlayerStats> _stats;
    std::unordered_map<ENetPeer*, bool>        _dirty;
};
