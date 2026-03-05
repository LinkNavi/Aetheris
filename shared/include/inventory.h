#pragma once
#include "item.h"
#include <array>

// ── Grid dimensions ────────────────────────────────────────────────────────────
static constexpr int INV_COLS  = 8;
static constexpr int INV_ROWS  = 5;
static constexpr int INV_SIZE  = INV_COLS * INV_ROWS; // 40 slots

// ── Hotbar ─────────────────────────────────────────────────────────────────────
static constexpr int HOTBAR_SIZE = 8;

// Each mode has its own independent 8-slot hotbar row.
// Cycle modes with Tab (or whatever key you bind in input handling).
enum class HotbarMode : uint8_t {
    Combat = 0, // weapons, shields, totems — weapon slot auto-syncs on switch
    Items  = 1, // consumables, potions, misc
    Blocks = 2, // building materials, ores
    COUNT  = 3,
};
static constexpr int HOTBAR_MODE_COUNT = (int)HotbarMode::COUNT;
static constexpr const char* HOTBAR_MODE_NAMES[] = { "Combat", "Items", "Blocks" };

// ── Equip slot indices (inside equipSlots array) ───────────────────────────────
static constexpr int EQUIP_WEAPON  = 0; // main hand weapon
static constexpr int EQUIP_OFFHAND = 1; // shield / parry item only
static constexpr int EQUIP_TOTEM_0 = 2; // 5 totem slots [2..6]
static constexpr int TOTEM_SLOTS   = 5;
static constexpr int EQUIP_SLOTS   = 2 + TOTEM_SLOTS; // 7

// ── SlotRegion — used by packets to identify which sub-array a slot is in ─────
enum class SlotRegion : uint8_t {
    Grid    = 0,
    Equip   = 1,
    Hotbar  = 2, // index = mode*HOTBAR_SIZE + slot
};

struct ItemStack {
    ItemID id    = ItemID::None;
    int    count = 0;
    bool empty() const { return id == ItemID::None || count <= 0; }
    void clear()       { id = ItemID::None; count = 0; }
};

struct Inventory {
    std::array<ItemStack, INV_SIZE>                       grid;
    std::array<ItemStack, EQUIP_SLOTS>                    equipSlots;
    std::array<ItemStack, HOTBAR_SIZE * HOTBAR_MODE_COUNT> hotbars; // 24 slots

    // ── Hotbar accessors ──────────────────────────────────────────────────────
    ItemStack& hotbarSlot(HotbarMode mode, int slot) {
        return hotbars[(int)mode * HOTBAR_SIZE + slot];
    }
    const ItemStack& hotbarSlot(HotbarMode mode, int slot) const {
        return hotbars[(int)mode * HOTBAR_SIZE + slot];
    }
    int hotbarFlatIndex(HotbarMode mode, int slot) const {
        return (int)mode * HOTBAR_SIZE + slot;
    }

    // ── Equip accessors ───────────────────────────────────────────────────────
    ItemStack& weaponSlot()     { return equipSlots[EQUIP_WEAPON]; }
    ItemStack& offhandSlot()    { return equipSlots[EQUIP_OFFHAND]; }
    ItemStack& totemSlot(int i) { return equipSlots[EQUIP_TOTEM_0 + i]; }

    // ── Add to bag (returns leftover count) ───────────────────────────────────
    int add(ItemID itemId, int n) {
        const ItemDef& def = getItemDef(itemId);
        for (auto& s : grid) {
            if (s.id == itemId && s.count < def.maxStack) {
                int take = std::min(n, def.maxStack - s.count);
                s.count += take; n -= take;
                if (n == 0) return 0;
            }
        }
        for (auto& s : grid) {
            if (s.empty()) {
                int take = std::min(n, def.maxStack);
                s = {itemId, take}; n -= take;
                if (n == 0) return 0;
            }
        }
        return n;
    }

    bool remove(ItemID itemId, int n = 1) {
        if (count(itemId) < n) return false;
        for (auto& s : grid) {
            if (s.id == itemId) {
                int take = std::min(s.count, n);
                s.count -= take; n -= take;
                if (s.count == 0) s.clear();
                if (n == 0) return true;
            }
        }
        return true;
    }

    int count(ItemID itemId) const {
        int total = 0;
        for (const auto& s : grid) if (s.id == itemId) total += s.count;
        return total;
    }
    bool has(ItemID itemId, int n = 1) const { return count(itemId) >= n; }
    bool full() const {
        for (const auto& s : grid) if (s.empty()) return false;
        return true;
    }
};

// ── ECS components ─────────────────────────────────────────────────────────────
struct CInventory {
    Inventory  inv;
    bool       open         = false;
    HotbarMode hotbarMode   = HotbarMode::Combat;
    int        hotbarActive = 0; // [0..HOTBAR_SIZE-1] within current mode
};

struct CChest {
    Inventory inv;
    uint32_t  uid           = 0;
    float     interactRange = 3.5f;
};
