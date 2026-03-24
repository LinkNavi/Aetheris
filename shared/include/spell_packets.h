#pragma once
#include "packets.h"
#include <string>

enum class SpellPacketID : uint8_t {
    SpellCastReq    = 0x40, // client -> server
    SpellCastAck    = 0x41, // server -> client: cast accepted + effects
    SpellCastFail   = 0x42, // server -> client: why it failed
    RunePlaceReq    = 0x43, // client -> server
    RuneStateUpdate = 0x44, // server -> client: placed/triggered/removed
    ProjectileSpawn = 0x45, // server -> all clients in range
    ProjectileHit   = 0x46, // server -> all clients in range
};

// Client -> server: I want to cast this spell
struct SpellCastReqPacket {
    std::string spellName;  // "firebolt", "blink", etc
    float       aimX, aimY, aimZ;   // world position player is aiming at
    uint32_t    targetEntityId = 0; // 0 if no specific target

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellPacketID::SpellCastReq);
        writeU32(b, (uint32_t)spellName.size());
        b.insert(b.end(), spellName.begin(), spellName.end());
        writeF32(b, aimX); writeF32(b, aimY); writeF32(b, aimZ);
        writeU32(b, targetEntityId);
        return b;
    }
    static SpellCastReqPacket deserialize(const uint8_t* d, size_t) {
        SpellCastReqPacket p; size_t o = 1;
        uint32_t len = readU32(d, o);
        p.spellName.assign((const char*)d + o, len); o += len;
        p.aimX = readF32(d, o); p.aimY = readF32(d, o); p.aimZ = readF32(d, o);
        p.targetEntityId = readU32(d, o);
        return p;
    }
};

// Server -> client: cast went through, here's what happened
struct SpellCastAckPacket {
    std::string spellName;
    float       originX, originY, originZ;
    float       dirX, dirY, dirZ;
    uint8_t     hasProjectile = 0;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellPacketID::SpellCastAck);
        writeU32(b, (uint32_t)spellName.size());
        b.insert(b.end(), spellName.begin(), spellName.end());
        writeF32(b, originX); writeF32(b, originY); writeF32(b, originZ);
        writeF32(b, dirX);    writeF32(b, dirY);    writeF32(b, dirZ);
        writeU8(b, hasProjectile);
        return b;
    }
    static SpellCastAckPacket deserialize(const uint8_t* d, size_t) {
        SpellCastAckPacket p; size_t o = 1;
        uint32_t len = readU32(d, o);
        p.spellName.assign((const char*)d + o, len); o += len;
        p.originX = readF32(d, o); p.originY = readF32(d, o); p.originZ = readF32(d, o);
        p.dirX    = readF32(d, o); p.dirY    = readF32(d, o); p.dirZ    = readF32(d, o);
        p.hasProjectile = readU8(d, o);
        return p;
    }
};

// Server -> client: cast failed
struct SpellCastFailPacket {
    std::string reason; // "no_mana", "no_grimoire", "invalid_spell"

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellPacketID::SpellCastFail);
        writeU32(b, (uint32_t)reason.size());
        b.insert(b.end(), reason.begin(), reason.end());
        return b;
    }
    static SpellCastFailPacket deserialize(const uint8_t* d, size_t) {
        SpellCastFailPacket p; size_t o = 1;
        uint32_t len = readU32(d, o);
        p.reason.assign((const char*)d + o, len);
        return p;
    }
};

// Client -> server: place a rune at this surface position
struct RunePlaceReqPacket {
    std::string runeName;
    float       posX, posY, posZ;
    float       normalX, normalY, normalZ; // surface normal for orientation

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellPacketID::RunePlaceReq);
        writeU32(b, (uint32_t)runeName.size());
        b.insert(b.end(), runeName.begin(), runeName.end());
        writeF32(b, posX); writeF32(b, posY); writeF32(b, posZ);
        writeF32(b, normalX); writeF32(b, normalY); writeF32(b, normalZ);
        return b;
    }
    static RunePlaceReqPacket deserialize(const uint8_t* d, size_t) {
        RunePlaceReqPacket p; size_t o = 1;
        uint32_t len = readU32(d, o);
        p.runeName.assign((const char*)d + o, len); o += len;
        p.posX = readF32(d, o); p.posY = readF32(d, o); p.posZ = readF32(d, o);
        p.normalX = readF32(d, o); p.normalY = readF32(d, o); p.normalZ = readF32(d, o);
        return p;
    }
};

// Server -> all nearby clients: rune was placed, triggered, or removed
struct RuneStateUpdatePacket {
    enum class Action : uint8_t { Placed = 0, Triggered = 1, Removed = 2 };
    uint32_t runeId;
    Action   action;
    float    posX, posY, posZ;
    std::string runeName;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellPacketID::RuneStateUpdate);
        writeU32(b, runeId);
        writeU8(b, (uint8_t)action);
        writeF32(b, posX); writeF32(b, posY); writeF32(b, posZ);
        writeU32(b, (uint32_t)runeName.size());
        b.insert(b.end(), runeName.begin(), runeName.end());
        return b;
    }
    static RuneStateUpdatePacket deserialize(const uint8_t* d, size_t) {
        RuneStateUpdatePacket p; size_t o = 1;
        p.runeId = readU32(d, o);
        p.action = (Action)readU8(d, o);
        p.posX = readF32(d, o); p.posY = readF32(d, o); p.posZ = readF32(d, o);
        uint32_t len = readU32(d, o);
        p.runeName.assign((const char*)d + o, len);
        return p;
    }
};

// Server -> all nearby clients: a projectile is in flight
struct ProjectileSpawnPacket {
    uint32_t  projectileId;
    float     originX, originY, originZ;
    float     dirX,    dirY,    dirZ;
    float     speed;
    std::string spellName; // so client knows which visual to use

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellPacketID::ProjectileSpawn);
        writeU32(b, projectileId);
        writeF32(b, originX); writeF32(b, originY); writeF32(b, originZ);
        writeF32(b, dirX);    writeF32(b, dirY);    writeF32(b, dirZ);
        writeF32(b, speed);
        writeU32(b, (uint32_t)spellName.size());
        b.insert(b.end(), spellName.begin(), spellName.end());
        return b;
    }
    static ProjectileSpawnPacket deserialize(const uint8_t* d, size_t) {
        ProjectileSpawnPacket p; size_t o = 1;
        p.projectileId = readU32(d, o);
        p.originX = readF32(d, o); p.originY = readF32(d, o); p.originZ = readF32(d, o);
        p.dirX    = readF32(d, o); p.dirY    = readF32(d, o); p.dirZ    = readF32(d, o);
        p.speed   = readF32(d, o);
        uint32_t len = readU32(d, o);
        p.spellName.assign((const char*)d + o, len);
        return p;
    }
};

// Server -> all nearby clients: projectile hit something
struct ProjectileHitPacket {
    uint32_t projectileId;
    float    posX, posY, posZ;
    float    aoeRadius;         // 0 if no aoe
    uint8_t  damageType;        // maps to DamageType enum

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellPacketID::ProjectileHit);
        writeU32(b, projectileId);
        writeF32(b, posX); writeF32(b, posY); writeF32(b, posZ);
        writeF32(b, aoeRadius);
        writeU8(b, damageType);
        return b;
    }
    static ProjectileHitPacket deserialize(const uint8_t* d, size_t) {
        ProjectileHitPacket p; size_t o = 1;
        p.projectileId = readU32(d, o);
        p.posX = readF32(d, o); p.posY = readF32(d, o); p.posZ = readF32(d, o);
        p.aoeRadius = readF32(d, o);
        p.damageType = readU8(d, o);
        return p;
    }
};
