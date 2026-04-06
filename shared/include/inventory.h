#pragma once
#include "item.h"
#include <array>
#include <string>
// ── Grid dimensions
// ────────────────────────────────────────────────────────────
static constexpr int INV_COLS = 8;
static constexpr int INV_ROWS = 5;
static constexpr int INV_SIZE = INV_COLS * INV_ROWS; // 40 slots

// ── Hotbar
// ─────────────────────────────────────────────────────────────────────
static constexpr int HOTBAR_SIZE = 8;

// Each mode has its own independent 8-slot hotbar row.
// Cycle modes with Tab (or whatever key you bind in input handling).
enum class HotbarMode : uint8_t {
  Combat = 0, // weapons, shields, totems — weapon slot auto-syncs on switch
  Items = 1,  // consumables, potions, misc
  Blocks = 2, // building materials, ores
  COUNT = 3,
};
static constexpr int HOTBAR_MODE_COUNT = (int)HotbarMode::COUNT;
static constexpr const char *HOTBAR_MODE_NAMES[] = {"Combat", "Items",
                                                    "Blocks"};

// ── Equip slot indices (inside equipSlots array)
// ───────────────────────────────
static constexpr int EQUIP_WEAPON = 0;  // main hand weapon
static constexpr int EQUIP_OFFHAND = 1; // shield / parry item only
static constexpr int EQUIP_TOTEM_0 = 2; // 5 totem slots [2..6]
static constexpr int TOTEM_SLOTS = 5;
static constexpr int EQUIP_SLOTS = 2 + TOTEM_SLOTS; // 7

// ── SlotRegion — used by packets to identify which sub-array a slot is in
// ─────
enum class SlotRegion : uint8_t {
  Grid = 0,
  Equip = 1,
  Hotbar = 2, // index = mode*HOTBAR_SIZE + slot
};

struct ItemStack {
  ItemID id = ItemID::None;
  int count = 0;
  bool empty() const { return id == ItemID::None || count <= 0; }
  void clear() {
    id = ItemID::None;
    count = 0;
  }
};
static constexpr int SPELL_SLOTS = 5;
static constexpr int SPELL_BOOK_SIZE = 20;

struct SpellEntry {
  std::string name;        // internal name, maps to script key on server
  std::string displayName; // player chosen
  std::string source;      // AetherScript source stored client-side for editor
  float baseMana = 0.f;
  float castTime = 0.f;
  bool empty() const { return name.empty(); }
  void clear() {
    name.clear();
    displayName.clear();
    source.clear();
  }
};
struct Inventory {
  std::array<ItemStack, INV_SIZE> grid;
  std::array<ItemStack, EQUIP_SLOTS> equipSlots;
  std::array<ItemStack, HOTBAR_SIZE * HOTBAR_MODE_COUNT> hotbars;
  // spell loadout — stored separately from item inventory
  std::array<SpellEntry, SPELL_BOOK_SIZE> spellBook{};
  std::array<int, SPELL_SLOTS> activeSpells{-1, -1, -1, -1, -1};



  // helpers
  SpellEntry *getActiveSpell(int slot) {
    if (slot < 0 || slot >= SPELL_SLOTS)
      return nullptr;
    int idx = activeSpells[slot];
    if (idx < 0 || idx >= SPELL_BOOK_SIZE)
      return nullptr;
    return &spellBook[idx];
  }

  int findSpellInBook(const std::string &name) const {
    for (int i = 0; i < SPELL_BOOK_SIZE; i++)
      if (spellBook[i].name == name)
        return i;
    return -1;
  }

  int firstEmptyBookSlot() const {
    for (int i = 0; i < SPELL_BOOK_SIZE; i++)
      if (spellBook[i].empty())
        return i;
    return -1;
  }
  ItemStack &hotbarSlot(HotbarMode mode, int slot) {
    return hotbars[(int)mode * HOTBAR_SIZE + slot];
  }
  const ItemStack &hotbarSlot(HotbarMode mode, int slot) const {
    return hotbars[(int)mode * HOTBAR_SIZE + slot];
  }
  int hotbarFlatIndex(HotbarMode mode, int slot) const {
    return (int)mode * HOTBAR_SIZE + slot;
  }

  ItemStack &weaponSlot() { return equipSlots[EQUIP_WEAPON]; }
  const ItemStack &weaponSlot() const { return equipSlots[EQUIP_WEAPON]; }

  ItemStack &offhandSlot() { return equipSlots[EQUIP_OFFHAND]; }
  const ItemStack &offhandSlot() const { return equipSlots[EQUIP_OFFHAND]; }

  ItemStack &totemSlot(int i) { return equipSlots[EQUIP_TOTEM_0 + i]; }
  const ItemStack &totemSlot(int i) const {
    return equipSlots[EQUIP_TOTEM_0 + i];
  }

  int add(ItemID itemId, int n) {
    const ItemDef &def = getItemDef(itemId);
    for (auto &s : grid) {
      if (s.id == itemId && s.count < def.maxStack) {
        int take = std::min(n, def.maxStack - s.count);
        s.count += take;
        n -= take;
        if (n == 0)
          return 0;
      }
    }
    for (auto &s : grid) {
      if (s.empty()) {
        int take = std::min(n, def.maxStack);
        s = {itemId, take};
        n -= take;
        if (n == 0)
          return 0;
      }
    }
    return n;
  }

  bool remove(ItemID itemId, int n = 1) {
    if (count(itemId) < n)
      return false;
    for (auto &s : grid) {
      if (s.id == itemId) {
        int take = std::min(s.count, n);
        s.count -= take;
        n -= take;
        if (s.count == 0)
          s.clear();
        if (n == 0)
          return true;
      }
    }
    return true;
  }

  int count(ItemID itemId) const {
    int total = 0;
    for (const auto &s : grid)
      if (s.id == itemId)
        total += s.count;
    return total;
  }
  bool has(ItemID itemId, int n = 1) const { return count(itemId) >= n; }
  bool full() const {
    for (const auto &s : grid)
      if (s.empty())
        return false;
    return true;
  }
};
// ── ECS components
// ─────────────────────────────────────────────────────────────
struct CInventory {
  Inventory inv;
  bool open = false;
  HotbarMode hotbarMode = HotbarMode::Combat;
  int hotbarActive = 0;
  int activeSpellSlot = 0; // [0..SPELL_SLOTS-1]
};

struct CChest {
  Inventory inv;
  uint32_t uid = 0;
  float interactRange = 3.5f;
};
