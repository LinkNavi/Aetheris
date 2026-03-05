#pragma once
#include <cstdint>
#include <string_view>
#include <array>

// ── Item types ────────────────────────────────────────────────────────────────
enum class ItemType : uint8_t {
    None,
    Weapon,
    Totem,
    Ore,
    Ingredient,
    Consumable,
    Shield,
    Material,
    KeyItem,
};

// ── Item IDs ──────────────────────────────────────────────────────────────────
// Add new items here. Keep None = 0.
enum class ItemID : uint16_t {
    None = 0,

    // ── Weapons ───────────────────────────────────────────────────────────────
    WpnSword,
    WpnDagger,
    WpnSpear,
    WpnBow,
    WpnStaff,

    // ── Ores (Overworld) ──────────────────────────────────────────────────────
    OreAshfen,
    OreGreyveil,
    OreDuskram,
    OreEmberrak,
    OreScurvite,
    OreThornvel,

    // ── Ores (Cave) ───────────────────────────────────────────────────────────
    OreMurkvein,
    OreGloomite,
    OreVaulmar,
    OreHollowfen,
    OreAbyssram,
    OreCryptveil,

    // ── Ingredients ───────────────────────────────────────────────────────────
    IngFlint,
    IngWyvWing,
    IngSovShard,    // Aldric drop

    // ── Consumables ───────────────────────────────────────────────────────────
    ConHealPotion,
    ConRevToken,    // Revival token — max stack 1

    // ── Totems ────────────────────────────────────────────────────────────────
    TotShade,
    TotVeil,
    TotWraith,
    TotBulwark,
    TotCounter,
    TotIronwall,
    TotEmber,
    TotFrost,
    TotVoid,
    TotHatch,
    TotLeash,
    TotSoulbond,
    TotPierce,
    TotSplit,
    TotTrap,
    TotFarsight,
    TotHunter,
    TotMark,
    TotMend,
    TotVigil,
    TotGrace,
    TotBarrier,
    TotWarding,
    TotAnchor,
    TotFlask,
    TotSplash,
    TotToxin,
    TotFocus,
    TotEndure,
    TotStrike,

    COUNT
};

static constexpr int ITEM_COUNT = (int)ItemID::COUNT;

// ── Item definition ───────────────────────────────────────────────────────────
struct ItemDef {
    std::string_view name;
    std::string_view description;
    ItemType         type;
    int              maxStack;  // 1 for weapons/totems, 64 for ores etc.
};

// ── Static definition table ───────────────────────────────────────────────────
// Indexed by (int)ItemID
inline constexpr std::array<ItemDef, ITEM_COUNT> ITEM_DEFS = {{
    // None
    {"None",          "",                                     ItemType::None,        0},

    // Weapons
    {"Sword",         "A straight-bladed melee weapon.",      ItemType::Weapon,      1},
    {"Dagger",        "Fast, short reach.",                   ItemType::Weapon,      1},
    {"Spear",         "Long reach, moderate speed.",          ItemType::Weapon,      1},
    {"Bow",           "Ranged. Consumes stamina to draw.",    ItemType::Weapon,      1},
    {"Staff",         "Magic catalyst.",                      ItemType::Weapon,      1},

    // Ores — Overworld
    {"Ashfen Ore",    "High durability. Lifesteal trait.",    ItemType::Ore,        64},
    {"Greyveil Ore",  "Balanced. Slow on hit.",               ItemType::Ore,        64},
    {"Duskram Ore",   "High speed. Bleed on hit.",            ItemType::Ore,        64},
    {"Emberrak Ore",  "High durability. Stamina reduction.",  ItemType::Ore,        64},
    {"Scorvite Ore",  "Very high speed. Burn on hit.",        ItemType::Ore,        64},
    {"Thornvel Ore",  "Very high durability. Extended bleed.",ItemType::Ore,        64},

    // Ores — Cave
    {"Murkvein Ore",  "High durability. Blindness on hit.",   ItemType::Ore,        64},
    {"Gloomite Ore",  "High speed. Sanity drain aura.",       ItemType::Ore,        64},
    {"Vaulmar Ore",   "Balanced. Echo damage.",               ItemType::Ore,        64},
    {"Hollowfen Ore", "High durability. Lifesteal.",          ItemType::Ore,        64},
    {"Abyssram Ore",  "High speed. Fear on hit.",             ItemType::Ore,        64},
    {"Cryptveil Ore", "High durability. Stacking poison.",    ItemType::Ore,        64},

    // Ingredients
    {"Flint",         "Used to craft basic tools.",           ItemType::Ingredient, 64},
    {"Wyvern Wing",   "Required to craft a glider.",          ItemType::Material,    4},
    {"Sovereign Shard","From the ruins of Aldric's campaign.",ItemType::Material,    4},

    // Consumables
    {"Heal Potion",   "Restores health.",                     ItemType::Consumable, 16},
    {"Revival Token", "Revives self or teammate. Rare.",      ItemType::KeyItem,     1},

    // Totems
    {"Shade Totem",   "Disables parry. Buffs dodge.",         ItemType::Totem,       1},
    {"Veil Totem",    "Dodge near enemy reduces aggro.",       ItemType::Totem,       1},
    {"Wraith Totem",  "Active: short invisibility.",          ItemType::Totem,       1},
    {"Bulwark Totem", "Parry restores stamina.",              ItemType::Totem,       1},
    {"Counter Totem", "Extends counter window after parry.",  ItemType::Totem,       1},
    {"Ironwall Totem","Active: shield bash staggers enemy.",  ItemType::Totem,       1},
    {"Ember Totem",   "Active: coat weapon in fire.",         ItemType::Totem,       1},
    {"Frost Totem",   "Active: coat weapon in frost.",        ItemType::Totem,       1},
    {"Void Totem",    "Active: coat weapon in void.",         ItemType::Totem,       1},
    {"Hatch Totem",   "Eggs incubate faster.",                ItemType::Totem,       1},
    {"Leash Totem",   "Active: focus companions on target.",  ItemType::Totem,       1},
    {"Soulbond Totem","Companion survives lethal hit once.",  ItemType::Totem,       1},
    {"Pierce Totem",  "Arrows penetrate first enemy hit.",   ItemType::Totem,       1},
    {"Split Totem",   "Active: next arrow splits into 3.",    ItemType::Totem,       1},
    {"Trap Totem",    "Active: fire a proximity trap arrow.", ItemType::Totem,       1},
    {"Farsight Totem","Reduces bow sway at full draw.",       ItemType::Totem,       1},
    {"Hunter Totem",  "Weak point shown after enemy attack.", ItemType::Totem,       1},
    {"Mark Totem",    "Active: mark target, +dmg for 8s.",    ItemType::Totem,       1},
    {"Mend Totem",    "Active: pulse heal to nearby allies.", ItemType::Totem,       1},
    {"Vigil Totem",   "Allies regen sanity faster nearby.",   ItemType::Totem,       1},
    {"Grace Totem",   "Revived allies get damage reduction.", ItemType::Totem,       1},
    {"Barrier Totem", "Active: place damage-absorbing field.",ItemType::Totem,       1},
    {"Warding Totem", "Enemies slowed after hitting your ward.",ItemType::Totem,     1},
    {"Anchor Totem",  "No stagger inside your own ward.",     ItemType::Totem,       1},
    {"Flask Totem",   "Active: craft heal potion instantly.", ItemType::Totem,       1},
    {"Splash Totem",  "Thrown potions have larger AoE.",      ItemType::Totem,       1},
    {"Toxin Totem",   "Poison applies faster and lasts longer.",ItemType::Totem,     1},
    {"Focus Totem",   "Discipline fills faster on parry.",    ItemType::Totem,       1},
    {"Endure Totem",  "Discipline heals restore more HP.",    ItemType::Totem,       1},
    {"Strike Totem",  "5 consecutive hits = attack speed buff.",ItemType::Totem,     1},
}};

inline const ItemDef& getItemDef(ItemID id) {
    int i = (int)id;
    if (i < 0 || i >= ITEM_COUNT) return ITEM_DEFS[0];
    return ITEM_DEFS[i];
}
