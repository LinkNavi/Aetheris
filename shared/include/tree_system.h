#pragma once
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>
#include "tree_gen.h"
#include "chunk.h"

// Key for looking up trees by their base world position
struct TreeKey {
    int wx, wz;
    bool operator==(const TreeKey& o) const { return wx==o.wx && wz==o.wz; }
};
struct TreeKeyHash {
    size_t operator()(const TreeKey& k) const {
        return std::hash<int64_t>{}(((int64_t)k.wx << 32) | (uint32_t)k.wz);
    }
};

// ── TreeSystem ─────────────────────────────────────────────────────────────────
// Manages tree health, damage, falling, and removal.
// Server-side. Client gets chunk updates when a tree falls/dies.
class TreeSystem {
public:
    static constexpr float TREE_MAX_HEALTH    = 100.f;
    static constexpr float FALL_SPEED         = 45.f;  // degrees per second
    static constexpr float FALL_MAX_ANGLE     = 90.f;  // degrees
    static constexpr float HIT_IFRAMES        = 0.3f;  // seconds between hits

    explicit TreeSystem(const TreeLibrary& lib) : _lib(lib) {}

    // Register a tree that exists in the world (called when chunk is generated)
    void registerTree(int wx, int wy, int wz, int templateIdx) {
        TreeKey key{wx, wz};
        if (_trees.count(key)) return; // already registered
        TreeInstance inst;
        inst.templateIdx = templateIdx;
        inst.wx = (float)wx;
        inst.wy = (float)wy;
        inst.wz = (float)wz;
        inst.health    = TREE_MAX_HEALTH;
        inst.maxHealth = TREE_MAX_HEALTH;
        _trees[key] = inst;
    }

    // Called when a player hits a tree (stub — no items yet)
    // Returns true if tree just died
    bool hitTree(int wx, int wz, float damage, float attackYaw) {
        TreeKey key{wx, wz};
        auto it = _trees.find(key);
        if (it == _trees.end()) return false;
        TreeInstance& tree = it->second;
        if (tree.dead || tree.falling) return false;

        // I-frame check
        if (tree.hitCooldown > 0.f) return false;
        tree.hitCooldown = HIT_IFRAMES;

        tree.health -= damage;
        if (tree.health <= 0.f) {
            tree.health  = 0.f;
            tree.dead    = true;
            tree.falling = true;
            // Fall away from attacker
            tree.fallDir = attackYaw + 180.f;
            // TODO: drop items here (stub)
            onTreeDeath(tree);
            return true;
        }
        return false;
    }

    // Per-frame update — advances fall animation
    // Returns list of trees that need chunk remesh this frame
    std::vector<TreeKey> update(float dt) {
        std::vector<TreeKey> dirty;
        for (auto& [key, tree] : _trees) {
            if (tree.hitCooldown > 0.f) tree.hitCooldown -= dt;

            if (!tree.falling || tree.fallen) continue;

            tree.fallAngle += FALL_SPEED * dt;
            if (tree.fallAngle >= FALL_MAX_ANGLE) {
                tree.fallAngle = FALL_MAX_ANGLE;
                tree.fallen    = true;
                tree.falling   = false;
            }
            dirty.push_back(key);
        }
        return dirty;
    }

    // Get fall transform for rendering (client side)
    // Returns rotation angle in degrees around horizontal axis
    bool getFallState(int wx, int wz, float& outAngle, float& outDir) const {
        TreeKey key{wx, wz};
        auto it = _trees.find(key);
        if (it == _trees.end()) return false;
        outAngle = it->second.fallAngle;
        outDir   = it->second.fallDir;
        return true;
    }

    TreeInstance* getTree(int wx, int wz) {
        TreeKey key{wx, wz};
        auto it = _trees.find(key);
        return it != _trees.end() ? &it->second : nullptr;
    }

    // Get health as 0-1 fraction (for HUD/crosshair)
    float getHealthFrac(int wx, int wz) const {
        TreeKey key{wx, wz};
        auto it = _trees.find(key);
        if (it == _trees.end()) return 1.f;
        return it->second.health / it->second.maxHealth;
    }

    bool isDead(int wx, int wz) const {
        TreeKey key{wx, wz};
        auto it = _trees.find(key);
        return it != _trees.end() && it->second.dead;
    }

private:
    const TreeLibrary& _lib;
    std::unordered_map<TreeKey, TreeInstance, TreeKeyHash> _trees;

    // Called when a tree dies — stub for item drops
    void onTreeDeath(const TreeInstance& tree) {
        // TODO: spawn wood/log items at tree.wx, tree.wy, tree.wz
        // TODO: send loot packet to nearby players
        (void)tree;
    }
};
