#pragma once
#include <enet/enet.h>
#include <unordered_map>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <cstdint>
#include <string>
#include <fstream>
#include <filesystem>
#include "inventory.h"
#include "inv_packets.h"
#include "net_common.h"
#include "log.h"

struct ServerChest {
    uint32_t  uid;
    glm::vec3 pos;
    Inventory inv;
    ENetPeer* lockedBy = nullptr;
};

struct CorpseLoot {
    uint32_t  uid;
    glm::vec3 pos;
    Inventory inv;
    ENetPeer* killer = nullptr;
};

class InventoryManager {
public:
    static constexpr float       CHEST_INTERACT_RANGE = 3.5f;
    static constexpr float       CORPSE_LOOT_RANGE    = 3.5f;
    static constexpr const char* CHEST_FILE           = "chests.dat";
    static constexpr const char* PLAYER_INV_DIR       = "player_invs/";

    InventoryManager() {
        std::filesystem::create_directories(PLAYER_INV_DIR);
        loadChests();
    }
    ~InventoryManager() { saveChests(); }

    // ── Player lifecycle ──────────────────────────────────────────────────────

    void onPlayerConnect(ENetPeer* peer, uint64_t uid) {
        _playerUIDs[peer] = uid;
        _playerInvs[peer] = loadPlayerInv(uid);
    }

    void onPlayerDisconnect(ENetPeer* peer) {
        auto it = _playerUIDs.find(peer);
        if (it != _playerUIDs.end()) {
            savePlayerInv(it->second, _playerInvs[peer]);
            _playerUIDs.erase(it);
        }
        _playerInvs.erase(peer);
        _playerPos.erase(peer);
        for (auto& [uid, chest] : _chests)
            if (chest.lockedBy == peer) chest.lockedBy = nullptr;
    }

    void onPlayerMove(ENetPeer* peer, glm::vec3 pos) {
        _playerPos[peer] = pos;
        for (auto& [uid, chest] : _chests) {
            if (chest.lockedBy == peer &&
                glm::length(pos - chest.pos) > CHEST_INTERACT_RANGE * 1.5f) {
                chest.lockedBy = nullptr;
            }
        }
    }

    void sendInventoryState(ENetPeer* peer) {
        auto it = _playerInvs.find(peer);
        if (it == _playerInvs.end()) return;
        InventoryStatePacket p; p.inv = it->second;
        Net::sendReliable(peer, p.serialize());
    }

    // ── Death / corpse ────────────────────────────────────────────────────────

    uint32_t onPlayerDeath(ENetPeer* dead, glm::vec3 pos, ENetPeer* killer) {
        auto it = _playerInvs.find(dead);
        if (it == _playerInvs.end()) return 0;

        uint32_t uid = _nextUID++;
        CorpseLoot loot{uid, pos, it->second, killer};
        _corpses[uid] = loot;
        it->second    = Inventory{};

        LootAvailablePacket lp{uid, pos};
        if (killer) {
            Net::sendReliable(killer, lp.serialize());
        } else {
            for (auto& [p, ppos] : _playerPos)
                if (glm::length(ppos - pos) < 30.f)
                    Net::sendReliable(p, lp.serialize());
        }
        return uid;
    }

    // ── Chest management ──────────────────────────────────────────────────────

    uint32_t addChest(glm::vec3 pos, Inventory prefill = {}) {
        uint32_t uid = _nextUID++;
        _chests[uid] = {uid, pos, prefill, nullptr};
        saveChests();
        return uid;
    }

    // ── Packet handlers ───────────────────────────────────────────────────────

    void onChestOpenReq(ENetPeer* peer, const ChestOpenReqPacket& req) {
        auto pit = _playerPos.find(peer);
        auto cit = _chests.find(req.chestUID);
        if (pit == _playerPos.end() || cit == _chests.end()) return;

        ServerChest& c = cit->second;
        if (glm::length(pit->second - c.pos) > CHEST_INTERACT_RANGE) return;
        if (c.lockedBy && c.lockedBy != peer) return;

        c.lockedBy = peer;
        ChestStatePacket p{c.uid, c.pos, c.inv};
        Net::sendReliable(peer, p.serialize());
    }

    void onChestCloseReq(ENetPeer* peer, const ChestCloseReqPacket& req) {
        auto it = _chests.find(req.chestUID);
        if (it != _chests.end() && it->second.lockedBy == peer)
            it->second.lockedBy = nullptr;
    }

    void onInventoryMoveReq(ENetPeer* peer, const InventoryMoveReqPacket& req) {
        Inventory* playerInv = getPlayerInv(peer);
        if (!playerInv) return;

        Inventory* srcInv = resolveInv(peer, req.src);
        Inventory* dstInv = resolveInv(peer, req.dst);
        if (!srcInv || !dstInv) {
            Log::warn("MoveReq: invalid inventory"); return;
        }

        ItemStack* srcSlot = resolveSlot(srcInv, req.src);
        ItemStack* dstSlot = resolveSlot(dstInv, req.dst);
        if (!srcSlot || !dstSlot) {
            Log::warn("MoveReq: invalid slot"); return;
        }

        if (!validateMove(*srcSlot, req.dst) ||
            !validateMove(*dstSlot, req.src)) {
            Log::warn("MoveReq: type guard failed"); return;
        }

        // Stack merge for same item in grid/hotbar
        if (req.src.region != SlotRegion::Equip &&
            req.dst.region != SlotRegion::Equip &&
            !srcSlot->empty() && !dstSlot->empty() &&
            srcSlot->id == dstSlot->id) {
            const ItemDef& def = getItemDef(srcSlot->id);
            int take = std::min(srcSlot->count, def.maxStack - dstSlot->count);
            dstSlot->count += take;
            srcSlot->count -= take;
            if (srcSlot->count <= 0) srcSlot->clear();
        } else {
            std::swap(*srcSlot, *dstSlot);
        }

        savePlayerInv(_playerUIDs[peer], *playerInv);

        // Build ack
        InventoryMoveAckPacket ack;
        ack.playerInv = *playerInv;

        uint32_t secUID = 0;
        Inventory* secInv = nullptr;
        if (req.src.owner == InvOwner::Chest || req.dst.owner == InvOwner::Chest) {
            secUID = (req.src.owner == InvOwner::Chest) ? req.src.uid : req.dst.uid;
            auto it = _chests.find(secUID);
            if (it != _chests.end()) { secInv = &it->second.inv; saveChests(); }
        } else if (req.src.owner == InvOwner::Corpse || req.dst.owner == InvOwner::Corpse) {
            secUID = (req.src.owner == InvOwner::Corpse) ? req.src.uid : req.dst.uid;
            auto it = _corpses.find(secUID);
            if (it != _corpses.end()) secInv = &it->second.inv;
        }

        if (secInv) {
            ack.hasSecondary = true;
            ack.secondaryUID = secUID;
            ack.secondaryInv = *secInv;
        }

        Net::sendReliable(peer, ack.serialize());

        // Clean up empty corpse
        if (req.src.owner == InvOwner::Corpse || req.dst.owner == InvOwner::Corpse)
            tryCleanCorpse(secUID);
    }

private:
    std::unordered_map<ENetPeer*, Inventory>  _playerInvs;
    std::unordered_map<ENetPeer*, uint64_t>   _playerUIDs;
    std::unordered_map<ENetPeer*, glm::vec3>  _playerPos;
    std::unordered_map<uint32_t, ServerChest> _chests;
    std::unordered_map<uint32_t, CorpseLoot>  _corpses;
    uint32_t _nextUID = 1;

    Inventory* getPlayerInv(ENetPeer* peer) {
        auto it = _playerInvs.find(peer);
        return it != _playerInvs.end() ? &it->second : nullptr;
    }

    Inventory* resolveInv(ENetPeer* peer, const SlotRef& ref) {
        switch (ref.owner) {
        case InvOwner::Player: return getPlayerInv(peer);
        case InvOwner::Chest: {
            auto it = _chests.find(ref.uid);
            if (it == _chests.end() || it->second.lockedBy != peer) return nullptr;
            return &it->second.inv;
        }
        case InvOwner::Corpse: {
            auto it = _corpses.find(ref.uid);
            if (it == _corpses.end()) return nullptr;
            if (it->second.killer && it->second.killer != peer) return nullptr;
            return &it->second.inv;
        }
        }
        return nullptr;
    }

    ItemStack* resolveSlot(Inventory* inv, const SlotRef& ref) {
        if (!inv) return nullptr;
        switch (ref.region) {
        case SlotRegion::Grid:
            if (ref.index >= INV_SIZE) return nullptr;
            return &inv->grid[ref.index];
        case SlotRegion::Equip:
            if (ref.index >= EQUIP_SLOTS) return nullptr;
            return &inv->equipSlots[ref.index];
        case SlotRegion::Hotbar:
            if (ref.index >= HOTBAR_SIZE * HOTBAR_MODE_COUNT) return nullptr;
            return &inv->hotbars[ref.index];
        }
        return nullptr;
    }

    // Type guard: can 'stack' go into slot 'dst'?
    bool validateMove(const ItemStack& stack, const SlotRef& dst) {
        if (dst.region != SlotRegion::Equip) return true; // grid/hotbar accept anything
        if (stack.empty()) return true;
        const ItemDef& def = getItemDef(stack.id);
        if (dst.index == EQUIP_WEAPON)  return def.type == ItemType::Weapon;
        if (dst.index == EQUIP_OFFHAND) return def.type == ItemType::Shield;
        if (dst.index >= EQUIP_TOTEM_0 &&
            dst.index <  EQUIP_TOTEM_0 + TOTEM_SLOTS)
            return def.type == ItemType::Totem;
        return false;
    }

    // ── Persistence ───────────────────────────────────────────────────────────

    void writeInv(std::ofstream& f, const Inventory& inv) {
        auto writeSlot = [&](const ItemStack& s) {
            uint16_t id = (uint16_t)s.id;
            int32_t  ct = s.count;
            f.write((char*)&id, 2);
            f.write((char*)&ct, 4);
        };
        for (const auto& s : inv.grid)       writeSlot(s);
        for (const auto& s : inv.equipSlots) writeSlot(s);
        for (const auto& s : inv.hotbars)    writeSlot(s);
    }

    void readInv(std::ifstream& f, Inventory& inv) {
        auto readSlot = [&](ItemStack& s) {
            uint16_t id; int32_t ct;
            f.read((char*)&id, 2); f.read((char*)&ct, 4);
            s.id = (ItemID)id; s.count = ct;
        };
        for (auto& s : inv.grid)       readSlot(s);
        for (auto& s : inv.equipSlots) readSlot(s);
        for (auto& s : inv.hotbars)    readSlot(s);
    }

    void saveChests() {
        std::ofstream f(CHEST_FILE, std::ios::binary | std::ios::trunc);
        if (!f) return;
        uint32_t cnt = (uint32_t)_chests.size();
        f.write((char*)&cnt,      4);
        f.write((char*)&_nextUID, 4);
        for (auto& [uid, c] : _chests) {
            f.write((char*)&c.uid,   4);
            f.write((char*)&c.pos.x, 4);
            f.write((char*)&c.pos.y, 4);
            f.write((char*)&c.pos.z, 4);
            writeInv(f, c.inv);
        }
    }

    void loadChests() {
        std::ifstream f(CHEST_FILE, std::ios::binary);
        if (!f) return;
        uint32_t cnt = 0;
        f.read((char*)&cnt,      4);
        f.read((char*)&_nextUID, 4);
        for (uint32_t i = 0; i < cnt; i++) {
            ServerChest c;
            f.read((char*)&c.uid,   4);
            f.read((char*)&c.pos.x, 4);
            f.read((char*)&c.pos.y, 4);
            f.read((char*)&c.pos.z, 4);
            readInv(f, c.inv);
            _chests[c.uid] = c;
        }
        Log::info("Loaded " + std::to_string(cnt) + " chests");
    }

    void savePlayerInv(uint64_t uid, const Inventory& inv) {
        std::string path = std::string(PLAYER_INV_DIR) + std::to_string(uid) + ".inv";
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f) writeInv(f, inv);
    }

    Inventory loadPlayerInv(uint64_t uid) {
        std::string path = std::string(PLAYER_INV_DIR) + std::to_string(uid) + ".inv";
        std::ifstream f(path, std::ios::binary);
        Inventory inv;
        if (f) readInv(f, inv);
        return inv;
    }

    void tryCleanCorpse(uint32_t uid) {
        auto it = _corpses.find(uid);
        if (it == _corpses.end()) return;
        for (const auto& s : it->second.inv.grid)
            if (!s.empty()) return;
        _corpses.erase(it);
    }
};
