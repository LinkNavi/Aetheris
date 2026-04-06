#pragma once
#include "packets.h"
#include <string>
#include "spell_element.h"
enum class SpellPacketID : uint8_t {
  SpellCastReq = 0x40,    // client -> server
  SpellCastAck = 0x41,    // server -> client: cast accepted + effects
  SpellCastFail = 0x42,   // server -> client: why it failed
  RunePlaceReq = 0x43,    // client -> server
  RuneStateUpdate = 0x44, // server -> client: placed/triggered/removed
  ProjectileSpawn = 0x45, // server -> all clients in range
  ProjectileHit = 0x46,   // server -> all clients in range
 
};
enum class SpellBookPacketID : uint8_t {
    CompileReq   = 0x60,  // was 0x50, conflicts with ChatMessage
    CompileAck   = 0x61,  // was 0x51
    LoadoutSet   = 0x62,  // was 0x52
    BookSync     = 0x63,  // was 0x53
    DeleteReq    = 0x64,  // was 0x54
};
// Client -> server: I want to cast this spell
struct SpellCastReqPacket {
  std::string spellName;       // "firebolt", "blink", etc
  float aimX, aimY, aimZ;      // world position player is aiming at
  uint32_t targetEntityId = 0; // 0 if no specific target

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    writeU8(b, (uint8_t)SpellPacketID::SpellCastReq);
    writeU32(b, (uint32_t)spellName.size());
    b.insert(b.end(), spellName.begin(), spellName.end());
    writeF32(b, aimX);
    writeF32(b, aimY);
    writeF32(b, aimZ);
    writeU32(b, targetEntityId);
    return b;
  }
  static SpellCastReqPacket deserialize(const uint8_t *d, size_t) {
    SpellCastReqPacket p;
    size_t o = 1;
    uint32_t len = readU32(d, o);
    p.spellName.assign((const char *)d + o, len);
    o += len;
    p.aimX = readF32(d, o);
    p.aimY = readF32(d, o);
    p.aimZ = readF32(d, o);
    p.targetEntityId = readU32(d, o);
    return p;
  }
};

// Server -> client: cast went through, here's what happened
struct SpellCastAckPacket {
  std::string spellName;
  float originX, originY, originZ;
  float dirX, dirY, dirZ;
  uint8_t hasProjectile = 0;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    writeU8(b, (uint8_t)SpellPacketID::SpellCastAck);
    writeU32(b, (uint32_t)spellName.size());
    b.insert(b.end(), spellName.begin(), spellName.end());
    writeF32(b, originX);
    writeF32(b, originY);
    writeF32(b, originZ);
    writeF32(b, dirX);
    writeF32(b, dirY);
    writeF32(b, dirZ);
    writeU8(b, hasProjectile);
    return b;
  }
  static SpellCastAckPacket deserialize(const uint8_t *d, size_t) {
    SpellCastAckPacket p;
    size_t o = 1;
    uint32_t len = readU32(d, o);
    p.spellName.assign((const char *)d + o, len);
    o += len;
    p.originX = readF32(d, o);
    p.originY = readF32(d, o);
    p.originZ = readF32(d, o);
    p.dirX = readF32(d, o);
    p.dirY = readF32(d, o);
    p.dirZ = readF32(d, o);
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
  static SpellCastFailPacket deserialize(const uint8_t *d, size_t) {
    SpellCastFailPacket p;
    size_t o = 1;
    uint32_t len = readU32(d, o);
    p.reason.assign((const char *)d + o, len);
    return p;
  }
};

// Client -> server: place a rune at this surface position
struct RunePlaceReqPacket {
  std::string runeName;
  float posX, posY, posZ;
  float normalX, normalY, normalZ; // surface normal for orientation

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    writeU8(b, (uint8_t)SpellPacketID::RunePlaceReq);
    writeU32(b, (uint32_t)runeName.size());
    b.insert(b.end(), runeName.begin(), runeName.end());
    writeF32(b, posX);
    writeF32(b, posY);
    writeF32(b, posZ);
    writeF32(b, normalX);
    writeF32(b, normalY);
    writeF32(b, normalZ);
    return b;
  }
  static RunePlaceReqPacket deserialize(const uint8_t *d, size_t) {
    RunePlaceReqPacket p;
    size_t o = 1;
    uint32_t len = readU32(d, o);
    p.runeName.assign((const char *)d + o, len);
    o += len;
    p.posX = readF32(d, o);
    p.posY = readF32(d, o);
    p.posZ = readF32(d, o);
    p.normalX = readF32(d, o);
    p.normalY = readF32(d, o);
    p.normalZ = readF32(d, o);
    return p;
  }
};

// Server -> all nearby clients: rune was placed, triggered, or removed
struct RuneStateUpdatePacket {
  enum class Action : uint8_t { Placed = 0, Triggered = 1, Removed = 2 };
  uint32_t runeId;
  Action action;
  float posX, posY, posZ;
  std::string runeName;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    writeU8(b, (uint8_t)SpellPacketID::RuneStateUpdate);
    writeU32(b, runeId);
    writeU8(b, (uint8_t)action);
    writeF32(b, posX);
    writeF32(b, posY);
    writeF32(b, posZ);
    writeU32(b, (uint32_t)runeName.size());
    b.insert(b.end(), runeName.begin(), runeName.end());
    return b;
  }
  static RuneStateUpdatePacket deserialize(const uint8_t *d, size_t) {
    RuneStateUpdatePacket p;
    size_t o = 1;
    p.runeId = readU32(d, o);
    p.action = (Action)readU8(d, o);
    p.posX = readF32(d, o);
    p.posY = readF32(d, o);
    p.posZ = readF32(d, o);
    uint32_t len = readU32(d, o);
    p.runeName.assign((const char *)d + o, len);
    return p;
  }
};

struct ProjectileSpawnPacket {
  uint32_t projectileId;
  float originX, originY, originZ;
  float dirX, dirY, dirZ;
  float speed;
  SpellElement element;
  float radius;
  float lifetime;
  std::string spellName;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    writeU8(b, (uint8_t)SpellPacketID::ProjectileSpawn);
    writeU32(b, projectileId);
    writeF32(b, originX);
    writeF32(b, originY);
    writeF32(b, originZ);
    writeF32(b, dirX);
    writeF32(b, dirY);
    writeF32(b, dirZ);
    writeF32(b, speed);
    writeU8(b, (uint8_t)element);
    writeF32(b, radius);
    writeF32(b, lifetime);
    writeU32(b, (uint32_t)spellName.size());
    b.insert(b.end(), spellName.begin(), spellName.end());
    return b;
  }

  static ProjectileSpawnPacket deserialize(const uint8_t *d, size_t) {
    ProjectileSpawnPacket p;
    size_t o = 1;
    p.projectileId = readU32(d, o);
    p.originX = readF32(d, o);
    p.originY = readF32(d, o);
    p.originZ = readF32(d, o);
    p.dirX = readF32(d, o);
    p.dirY = readF32(d, o);
    p.dirZ = readF32(d, o);
    p.speed = readF32(d, o);
    p.element = (SpellElement)readU8(d, o);
    p.radius = readF32(d, o);
    p.lifetime = readF32(d, o);
    uint32_t len = readU32(d, o);
    p.spellName.assign((const char *)d + o, len);
    return p;
  }
};

// Server -> all nearby clients: projectile hit something
struct ProjectileHitPacket {
  uint32_t projectileId;
  float posX, posY, posZ;
  float aoeRadius;    // 0 if no aoe
  uint8_t damageType; // maps to DamageType enum

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    writeU8(b, (uint8_t)SpellPacketID::ProjectileHit);
    writeU32(b, projectileId);
    writeF32(b, posX);
    writeF32(b, posY);
    writeF32(b, posZ);
    writeF32(b, aoeRadius);
    writeU8(b, damageType);
    return b;
  }
  static ProjectileHitPacket deserialize(const uint8_t *d, size_t) {
    ProjectileHitPacket p;
    size_t o = 1;
    p.projectileId = readU32(d, o);
    p.posX = readF32(d, o);
    p.posY = readF32(d, o);
    p.posZ = readF32(d, o);
    p.aoeRadius = readF32(d, o);
    p.damageType = readU8(d, o);
    return p;
  }
};

// client -> server: "compile this script and add it to my spellbook"
struct SpellCompileReqPacket {
    std::string spellName;  // player-chosen display name
    std::string source;     // raw AetherScript

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellBookPacketID::CompileReq);
        writeU32(b, (uint32_t)spellName.size());
        b.insert(b.end(), spellName.begin(), spellName.end());
        writeU32(b, (uint32_t)source.size());
        b.insert(b.end(), source.begin(), source.end());
        return b;
    }
    static SpellCompileReqPacket deserialize(const uint8_t* d, size_t len) {
        SpellCompileReqPacket p; size_t o = 1;
        uint32_t nl = readU32(d, o);
        p.spellName.assign((const char*)d + o, nl); o += nl;
        uint32_t sl = readU32(d, o);
        p.source.assign((const char*)d + o, sl);
        return p;
    }
};

// server -> client: compile result
struct SpellCompileAckPacket {
    uint8_t     success   = 0;
    std::string spellName;
    std::string error;      // empty on success
    float       baseMana  = 0.f;
    float       castTime  = 0.f;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellBookPacketID::CompileAck);
        writeU8(b, success);
        writeU32(b, (uint32_t)spellName.size());
        b.insert(b.end(), spellName.begin(), spellName.end());
        writeU32(b, (uint32_t)error.size());
        b.insert(b.end(), error.begin(), error.end());
        writeF32(b, baseMana);
        writeF32(b, castTime);
        return b;
    }
    static SpellCompileAckPacket deserialize(const uint8_t* d, size_t len) {
        SpellCompileAckPacket p; size_t o = 1;
        p.success = readU8(d, o);
        uint32_t nl = readU32(d, o);
        p.spellName.assign((const char*)d + o, nl); o += nl;
        uint32_t el = readU32(d, o);
        p.error.assign((const char*)d + o, el); o += el;
        p.baseMana = readF32(d, o);
        p.castTime = readF32(d, o);
        return p;
    }
};

// client -> server: set which spells are in the active 5 slots
struct SpellLoadoutSetPacket {
    std::array<std::string, 5> slots; // spell names, empty string = empty slot

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellBookPacketID::LoadoutSet);
        for (int i = 0; i < 5; i++) {
            writeU32(b, (uint32_t)slots[i].size());
            b.insert(b.end(), slots[i].begin(), slots[i].end());
        }
        return b;
    }
    static SpellLoadoutSetPacket deserialize(const uint8_t* d, size_t len) {
        SpellLoadoutSetPacket p; size_t o = 1;
        for (int i = 0; i < 5; i++) {
            uint32_t sl = readU32(d, o);
            p.slots[i].assign((const char*)d + o, sl); o += sl;
        }
        return p;
    }
};

// server -> client: full spellbook sync on connect/reload
struct SpellBookSyncPacket {
    struct Entry {
        std::string name;
        std::string source;
        float       baseMana = 0.f;
        float       castTime = 0.f;
    };
    std::vector<Entry>             spells;
    std::array<std::string, 5>     activeSlots;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellBookPacketID::BookSync);
        writeU32(b, (uint32_t)spells.size());
        for (auto& e : spells) {
            writeU32(b, (uint32_t)e.name.size());
            b.insert(b.end(), e.name.begin(), e.name.end());
            writeU32(b, (uint32_t)e.source.size());
            b.insert(b.end(), e.source.begin(), e.source.end());
            writeF32(b, e.baseMana);
            writeF32(b, e.castTime);
        }
        for (int i = 0; i < 5; i++) {
            writeU32(b, (uint32_t)activeSlots[i].size());
            b.insert(b.end(), activeSlots[i].begin(), activeSlots[i].end());
        }
        return b;
    }
    static SpellBookSyncPacket deserialize(const uint8_t* d, size_t len) {
        SpellBookSyncPacket p; size_t o = 1;
        uint32_t count = readU32(d, o);
        p.spells.resize(count);
        for (auto& e : p.spells) {
            uint32_t nl = readU32(d, o);
            e.name.assign((const char*)d + o, nl); o += nl;
            uint32_t sl = readU32(d, o);
            e.source.assign((const char*)d + o, sl); o += sl;
            e.baseMana = readF32(d, o);
            e.castTime = readF32(d, o);
        }
        for (int i = 0; i < 5; i++) {
            uint32_t sl = readU32(d, o);
            p.activeSlots[i].assign((const char*)d + o, sl); o += sl;
        }
        return p;
    }
};

// client -> server: delete a spell from spellbook
struct SpellDeleteReqPacket {
    std::string spellName;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)SpellBookPacketID::DeleteReq);
        writeU32(b, (uint32_t)spellName.size());
        b.insert(b.end(), spellName.begin(), spellName.end());
        return b;
    }
    static SpellDeleteReqPacket deserialize(const uint8_t* d, size_t len) {
        SpellDeleteReqPacket p; size_t o = 1;
        uint32_t nl = readU32(d, o);
        p.spellName.assign((const char*)d + o, nl);
        return p;
    }
};
