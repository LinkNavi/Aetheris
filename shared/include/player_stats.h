#pragma once
#include <vector>
#include <cstdint>
#include "packets.h"

// ── Server-authoritative player stats ─────────────────────────────────────────

struct PlayerStats {
    float health    = 100.f;
    float healthMax = 100.f;
    float stamina    = 100.f;
    float staminaMax = 100.f;
    float mana       = 100.f;
    float manaMax    = 100.f;
    float armour     = 0.f;
    float armourMax  = 100.f;
    bool  dead       = false;

    void clamp() {
        if (health  < 0.f) health  = 0.f;
        if (health  > healthMax)  health  = healthMax;
        if (stamina < 0.f) stamina = 0.f;
        if (stamina > staminaMax) stamina = staminaMax;
        if (mana    < 0.f) mana    = 0.f;
        if (mana    > manaMax)    mana    = manaMax;
        if (armour  < 0.f) armour  = 0.f;
        if (armour  > armourMax)  armour  = armourMax;
        if (health <= 0.f) { health = 0.f; dead = true; }
    }

    void reset() {
        health  = healthMax;
        stamina = staminaMax;
        mana    = manaMax;
        armour  = 0.f;
        dead    = false;
    }
};

// ── Packet IDs for stats ──────────────────────────────────────────────────────

enum class StatsPacketID : uint8_t {
    StatsSync    = 0x20, // server -> client: full stats snapshot
    StatsDelta   = 0x21, // server -> client: delta update (frequent)
};

// Full stats sync — sent on connect, respawn, big changes
struct StatsSyncPacket {
    float health, healthMax;
    float stamina, staminaMax;
    float mana, manaMax;
    float armour, armourMax;
    uint8_t dead;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)StatsPacketID::StatsSync);
        writeF32(b, health);  writeF32(b, healthMax);
        writeF32(b, stamina); writeF32(b, staminaMax);
        writeF32(b, mana);    writeF32(b, manaMax);
        writeF32(b, armour);  writeF32(b, armourMax);
        writeU8(b, dead);
        return b;
    }

    static StatsSyncPacket deserialize(const uint8_t* d, size_t) {
        StatsSyncPacket p; size_t o = 1;
        p.health    = readF32(d, o); p.healthMax  = readF32(d, o);
        p.stamina   = readF32(d, o); p.staminaMax = readF32(d, o);
        p.mana      = readF32(d, o); p.manaMax    = readF32(d, o);
        p.armour    = readF32(d, o); p.armourMax  = readF32(d, o);
        p.dead      = readU8(d, o);
        return p;
    }

    static StatsSyncPacket from(const PlayerStats& s) {
        return {s.health, s.healthMax, s.stamina, s.staminaMax,
                s.mana, s.manaMax, s.armour, s.armourMax, (uint8_t)(s.dead ? 1 : 0)};
    }
};

// Compact delta — just the 4 current values + dead flag (sent at 10Hz)
struct StatsDeltaPacket {
    float health, stamina, mana, armour;
    uint8_t dead;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)StatsPacketID::StatsDelta);
        writeF32(b, health);
        writeF32(b, stamina);
        writeF32(b, mana);
        writeF32(b, armour);
        writeU8(b, dead);
        return b;
    }

    static StatsDeltaPacket deserialize(const uint8_t* d, size_t) {
        StatsDeltaPacket p; size_t o = 1;
        p.health  = readF32(d, o);
        p.stamina = readF32(d, o);
        p.mana    = readF32(d, o);
        p.armour  = readF32(d, o);
        p.dead    = readU8(d, o);
        return p;
    }

    static StatsDeltaPacket from(const PlayerStats& s) {
        return {s.health, s.stamina, s.mana, s.armour, (uint8_t)(s.dead ? 1 : 0)};
    }
};
