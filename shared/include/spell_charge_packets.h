#pragma once
// Add these to shared/include/spell_packets.h
// (or include this file alongside it)
#include "packets.h"

// ── New packet IDs — add to SpellPacketID enum ────────────────────────────────
// SpellChargeBegin  = 0x47  client -> server: started holding cast key
// SpellChargeTick   = 0x48  client -> server: still holding (sent at ~20hz)
// SpellChargeCommit = 0x49  client -> server: released key, commit cast
// SpellChargeCancel = 0x4A  client -> server: cancelled (tap / ESC)
// SpellCastState    = 0x4B  server -> client: wind-up progress + interrupt

enum class SpellChargePacketID : uint8_t {
    Begin   = 0x47,
    Tick    = 0x48,
    Commit  = 0x49,
    Cancel  = 0x4A,
    State   = 0x4B,
};

// Client → server: player started charging a spell
struct SpellChargeBeginPacket {
    std::string spellName;
    float aimX, aimY, aimZ;
    uint32_t targetId = 0;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellChargePacketID::Begin);
        writeU32(b, (uint32_t)spellName.size());
        b.insert(b.end(), spellName.begin(), spellName.end());
        writeF32(b, aimX); writeF32(b, aimY); writeF32(b, aimZ);
        writeU32(b, targetId);
        return b;
    }
    static SpellChargeBeginPacket deserialize(const uint8_t* d, size_t) {
        SpellChargeBeginPacket p; size_t o = 1;
        uint32_t len = readU32(d, o);
        p.spellName.assign((const char*)d + o, len); o += len;
        p.aimX = readF32(d, o); p.aimY = readF32(d, o); p.aimZ = readF32(d, o);
        p.targetId = readU32(d, o);
        return p;
    }
};

// Client → server: update aim while charging (20hz)
struct SpellChargeTickPacket {
    float aimX, aimY, aimZ;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellChargePacketID::Tick);
        writeF32(b, aimX); writeF32(b, aimY); writeF32(b, aimZ);
        return b;
    }
    static SpellChargeTickPacket deserialize(const uint8_t* d, size_t) {
        SpellChargeTickPacket p; size_t o = 1;
        p.aimX = readF32(d, o); p.aimY = readF32(d, o); p.aimZ = readF32(d, o);
        return p;
    }
};

// Client → server: released key — commit whatever mana was charged
struct SpellChargeCommitPacket {
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellChargePacketID::Commit);
        return b;
    }
    static SpellChargeCommitPacket deserialize(const uint8_t*, size_t) { return {}; }
};

// Client → server: cancelled cast before releasing
struct SpellChargeCancelPacket {
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellChargePacketID::Cancel);
        return b;
    }
    static SpellChargeCancelPacket deserialize(const uint8_t*, size_t) { return {}; }
};

// Server → client: current cast state (for UI progress bar + interrupt flash)
struct SpellCastStatePacket {
    uint8_t  phase;          // 0=idle 1=charging 2=windup 3=firing
    float    manaCommitted;
    float    castTimeTotal;
    float    castTimeElapsed;
    float    interruptDC;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellChargePacketID::State);
        writeU8(b, phase);
        writeF32(b, manaCommitted);
        writeF32(b, castTimeTotal);
        writeF32(b, castTimeElapsed);
        writeF32(b, interruptDC);
        return b;
    }
    static SpellCastStatePacket deserialize(const uint8_t* d, size_t) {
        SpellCastStatePacket p; size_t o = 1;
        p.phase           = readU8(d, o);
        p.manaCommitted   = readF32(d, o);
        p.castTimeTotal   = readF32(d, o);
        p.castTimeElapsed = readF32(d, o);
        p.interruptDC     = readF32(d, o);
        return p;
    }
};