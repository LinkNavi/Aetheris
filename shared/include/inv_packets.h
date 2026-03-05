#pragma once
#include "packets.h"
#include "inventory.h"

enum class InvPacketID : uint8_t {
    InventoryState   = 0x10,
    ChestOpenReq     = 0x11,
    ChestState       = 0x12,
    ChestCloseReq    = 0x13,
    InventoryMoveReq = 0x14,
    InventoryMoveAck = 0x15,
    LootAvailable    = 0x16,
    HotbarModeSync   = 0x17, // client -> server: active mode + slot changed
};

enum class InvOwner : uint8_t {
    Player = 0,
    Chest  = 1,
    Corpse = 2,
};

// Identifies any slot in any inventory
struct SlotRef {
    InvOwner   owner;
    uint32_t   uid;    // chest/corpse UID (ignored for Player)
    SlotRegion region; // Grid / Equip / Hotbar
    uint8_t    index;  // flat index within that region
};

// ── Serialization helpers ──────────────────────────────────────────────────────
inline void writeSlotRef(std::vector<uint8_t>& b, const SlotRef& s) {
    writeU8(b, (uint8_t)s.owner);
    writeU32(b, s.uid);
    writeU8(b, (uint8_t)s.region);
    writeU8(b, s.index);
}
inline SlotRef readSlotRef(const uint8_t* d, size_t& o) {
    SlotRef s;
    s.owner  = (InvOwner)readU8(d, o);
    s.uid    = readU32(d, o);
    s.region = (SlotRegion)readU8(d, o);
    s.index  = readU8(d, o);
    return s;
}
inline void writeStack(std::vector<uint8_t>& b, const ItemStack& s) {
    writeU32(b, (uint32_t)s.id);
    writeU32(b, (uint32_t)s.count);
}
inline ItemStack readStack(const uint8_t* d, size_t& o) {
    ItemStack s;
    s.id    = (ItemID)readU32(d, o);
    s.count = (int)readU32(d, o);
    return s;
}
inline void writeInventory(std::vector<uint8_t>& b, const Inventory& inv) {
    for (const auto& s : inv.grid)       writeStack(b, s);
    for (const auto& s : inv.equipSlots) writeStack(b, s);
    for (const auto& s : inv.hotbars)    writeStack(b, s);
}
inline void readInventory(const uint8_t* d, size_t& o, Inventory& inv) {
    for (auto& s : inv.grid)       s = readStack(d, o);
    for (auto& s : inv.equipSlots) s = readStack(d, o);
    for (auto& s : inv.hotbars)    s = readStack(d, o);
}

// ── Packets ────────────────────────────────────────────────────────────────────

struct InventoryStatePacket {
    Inventory inv;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)InvPacketID::InventoryState);
        writeInventory(b, inv);
        return b;
    }
    static InventoryStatePacket deserialize(const uint8_t* d, size_t) {
        InventoryStatePacket p; size_t o = 1;
        readInventory(d, o, p.inv);
        return p;
    }
};

struct ChestOpenReqPacket {
    uint32_t chestUID;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)InvPacketID::ChestOpenReq);
        writeU32(b, chestUID);
        return b;
    }
    static ChestOpenReqPacket deserialize(const uint8_t* d, size_t) {
        ChestOpenReqPacket p; size_t o = 1;
        p.chestUID = readU32(d, o);
        return p;
    }
};

struct ChestStatePacket {
    uint32_t  chestUID;
    glm::vec3 pos;
    Inventory inv;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)InvPacketID::ChestState);
        writeU32(b, chestUID);
        writeF32(b, pos.x); writeF32(b, pos.y); writeF32(b, pos.z);
        writeInventory(b, inv);
        return b;
    }
    static ChestStatePacket deserialize(const uint8_t* d, size_t) {
        ChestStatePacket p; size_t o = 1;
        p.chestUID = readU32(d, o);
        p.pos.x = readF32(d, o); p.pos.y = readF32(d, o); p.pos.z = readF32(d, o);
        readInventory(d, o, p.inv);
        return p;
    }
};

struct ChestCloseReqPacket {
    uint32_t chestUID;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)InvPacketID::ChestCloseReq);
        writeU32(b, chestUID);
        return b;
    }
    static ChestCloseReqPacket deserialize(const uint8_t* d, size_t) {
        ChestCloseReqPacket p; size_t o = 1;
        p.chestUID = readU32(d, o);
        return p;
    }
};

struct InventoryMoveReqPacket {
    SlotRef src;
    SlotRef dst;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)InvPacketID::InventoryMoveReq);
        writeSlotRef(b, src);
        writeSlotRef(b, dst);
        return b;
    }
    static InventoryMoveReqPacket deserialize(const uint8_t* d, size_t) {
        InventoryMoveReqPacket p; size_t o = 1;
        p.src = readSlotRef(d, o);
        p.dst = readSlotRef(d, o);
        return p;
    }
};

struct InventoryMoveAckPacket {
    Inventory playerInv;
    bool      hasSecondary = false;
    uint32_t  secondaryUID = 0;
    Inventory secondaryInv;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)InvPacketID::InventoryMoveAck);
        writeInventory(b, playerInv);
        writeU8(b, hasSecondary ? 1 : 0);
        if (hasSecondary) {
            writeU32(b, secondaryUID);
            writeInventory(b, secondaryInv);
        }
        return b;
    }
    static InventoryMoveAckPacket deserialize(const uint8_t* d, size_t) {
        InventoryMoveAckPacket p; size_t o = 1;
        readInventory(d, o, p.playerInv);
        p.hasSecondary = readU8(d, o) != 0;
        if (p.hasSecondary) {
            p.secondaryUID = readU32(d, o);
            readInventory(d, o, p.secondaryInv);
        }
        return p;
    }
};

struct LootAvailablePacket {
    uint32_t  corpseUID;
    glm::vec3 pos;
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> b;
        writeU8(b, (uint8_t)InvPacketID::LootAvailable);
        writeU32(b, corpseUID);
        writeF32(b, pos.x); writeF32(b, pos.y); writeF32(b, pos.z);
        return b;
    }
    static LootAvailablePacket deserialize(const uint8_t* d, size_t) {
        LootAvailablePacket p; size_t o = 1;
        p.corpseUID = readU32(d, o);
        p.pos.x = readF32(d, o); p.pos.y = readF32(d, o); p.pos.z = readF32(d, o);
        return p;
    }
};
