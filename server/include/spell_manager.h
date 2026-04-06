#pragma once
#include "aether.h"
#include "player_stats.h"
#include "inventory.h"
#include "net_common.h"
#include "spell_packets.h"
#include "log.h"
#include <enet/enet.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cmath>
#include <filesystem>

namespace Aether { struct Script; }

// ── Spell metadata (parsed from spell header fields) ─────────────────────────
struct SpellDef {
    std::string          name;
    float                baseMana     = 0.f;
    float                baseTime     = 0.2f;
    float                timeScale    = 1.0f;
    bool                 requireStaff = false;
    bool                 isRune       = false; // rune (placed) vs spell (cast)
    std::string          trigger;              // e.g. "on_enter", "on_activate"
    Aether::Script       script;
};

// ── Per-player cast state ─────────────────────────────────────────────────────
struct CastState {
    enum class Phase { Idle, Charging, WindUp, Firing };

    Phase       phase          = Phase::Idle;
float potencyBudget = 0.f;   // set when spell fires, consumed by primitives
    // Charging
    std::string spellName;
    float       manaCommitted  = 0.f;
    float       chargeRate     = 60.f;  // mana/sec while holding key
    float       maxCharge      = 200.f; // hard cap

    // Wind-up
    float       castTimeTotal  = 0.f;
    float       castTimeElapsed= 0.f;
    float       interruptDC    = 0.f;   // damage needed to cancel wind-up

    // Aim
    float       aimX=0, aimY=0, aimZ=0;
    uint32_t    targetId = 0;

    bool isIdle()     const { return phase == Phase::Idle; }
    bool isCharging() const { return phase == Phase::Charging; }
    bool isWindingUp()const { return phase == Phase::WindUp; }

    void reset() { *this = CastState{}; }

    // ── Scaling formulas ──────────────────────────────────────────────────────
    static float calcCastTime(float mana, float baseTime, float timeScale) {
        // cast_time = baseTime + (mana/100)^1.5 * timeScale
        float t = std::pow(mana / 100.f, 1.5f) * timeScale;
        return baseTime + t;
    }

    static float calcInterruptDC(float mana) {
        // interrupt_threshold = mana * 0.3
        // i.e. a 10-damage hit can interrupt a 33-mana spell
        return mana * 0.3f;
    }

    static float calcPotency(float mana) {
        // linear — cast time is the tax
        return mana;
    }
};

// ── SpellManager ──────────────────────────────────────────────────────────────
// Server-side. One instance. Owns all compiled spell scripts + per-player state.
class SpellManager {
public:
    // Load all .aes files from a directory
    void loadSpellsFromDir(const std::string& dir);
float*           getFiringBudget()       { return _isFiring ? &_firingBudget : nullptr; }
const CastState* getCurrentFiringState() { return _firingState; }
ENetPeer*        getCurrentFiringPeer()  { return _firingPeer; }
    // Load a single spell from source string (for preset spells)
    bool loadSpellSource(const std::string& name, const std::string& src);
struct SpellMeta { float baseMana; float castTime; };
SpellMeta getSpellMeta(const std::string& name) const {
    auto it = _spells.find(name);
    if (it == _spells.end()) return {20.f, 0.5f};
    return {it->second.baseMana, it->second.baseTime};
}
    // ── Per-frame update ──────────────────────────────────────────────────────
    // Call every server tick. Advances wind-up timers, fires spells on completion.
    // Returns list of peers whose cast completed this frame (for sending acks).
    struct FireEvent {
        ENetPeer*   peer;
        std::string spellName;
        float       manaSpent;
        float       potency;
        float       aimX, aimY, aimZ;
        uint32_t    targetId;
    };
    std::vector<FireEvent> update(float dt);


    // ── Player actions ────────────────────────────────────────────────────────

    // Called when player starts holding cast key
    bool beginCharge(ENetPeer* peer, const std::string& spellName,
                     float aimX, float aimY, float aimZ, uint32_t targetId,
                     const PlayerStats& stats, const Inventory& inv);

    // Called each frame player holds cast key (charges mana commitment)
    void tickCharge(ENetPeer* peer, float dt, PlayerStats& stats);

    // Called when player releases cast key — locks in mana and starts wind-up
    // Returns false if not enough mana / wrong state
    bool commitCast(ENetPeer* peer, PlayerStats& stats);

    // Cancel with no mana cost (tap without releasing, or ESC)
    void cancelCast(ENetPeer* peer);

    // Called when caster takes damage during wind-up
    // Returns true if cast was interrupted
    bool onDamageTaken(ENetPeer* peer, float damage, PlayerStats& stats);

    // Called on disconnect / death
    void onPlayerRemoved(ENetPeer* peer);

    // ── Cooldowns ─────────────────────────────────────────────────────────────
    bool isOnCooldown(ENetPeer* peer, const std::string& spell) const;
    float getCooldown(ENetPeer* peer, const std::string& spell) const;

    // ── Native function registry ──────────────────────────────────────────────
    // Call this to wire spell primitives into the VM before firing spells
    void registerNative(const std::string& name, Aether::NativeFn fn);

    // ── Rune triggering ───────────────────────────────────────────────────────
    // Call when a world event matches a rune's trigger type.
    // Returns the exec result so the caller can apply effects.
    Aether::ExecResult triggerRune(const std::string& name,
                                   const std::string& triggerType,
                                   std::vector<Aether::Value> args = {});

    bool isRune(const std::string& name) const;

    // ── State query ───────────────────────────────────────────────────────────
    const CastState* getCastState(ENetPeer* peer) const;
    bool             hasspell(const std::string& name) const;

private:
    std::unordered_map<std::string, SpellDef>    _spells;
    std::unordered_map<ENetPeer*, CastState>     _castStates;
    std::unordered_map<ENetPeer*,
        std::unordered_map<std::string, float>>  _cooldowns;
    std::vector<std::pair<std::string,Aether::NativeFn>> _pendingNatives;

float            _firingBudget = 0.f;
bool             _isFiring     = false;
const CastState* _firingState  = nullptr;
ENetPeer*        _firingPeer   = nullptr;
    SpellDef*   findSpell(const std::string& name);
    CastState&  stateFor(ENetPeer* peer);
    void        tickCooldowns(float dt);
    void        applyCooldown(ENetPeer* peer, const std::string& spell, float mana);
    bool        hasStaffEquipped(const Inventory& inv) const;

    // Compute cooldown from mana spent — bigger casts have longer cooldowns
    static float calcCooldown(float mana) {
        // cd = 0.5 + mana/40   →  10 mana=0.75s,  80 mana=2.5s,  200 mana=5.5s
        return 0.5f + mana / 40.f;
    }
};
