#pragma once
#include "aether.h"
#include "player_stats.h"
#include "inventory.h"
#include "net_common.h"
#include "spell_packets.h"
#include "spell_element.h"
#include "log.h"
#include <enet/enet.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cmath>
#include <filesystem>

namespace Aether { struct Script; }

struct SpellDef {
    std::string    name;
    float          baseMana     = 0.f;
    float          baseTime     = 0.2f;
    float          timeScale    = 1.0f;
    bool           requireStaff = false;
    bool           isRune       = false;
    std::string    trigger;
    SpellElement   element      = SpellElement::None; // add
    Aether::Script script;
};

struct CastState {
    enum class Phase { Idle, Charging, WindUp, Firing };

    Phase       phase           = Phase::Idle;
    float       potencyBudget  = 0.f;
    std::string spellName;
    float       manaCommitted  = 0.f;
    float       chargeRate     = 60.f;
    float       maxCharge      = 200.f;
    float       castTimeTotal  = 0.f;
    float       castTimeElapsed= 0.f;
    float       interruptDC    = 0.f;
    float       aimX=0, aimY=0, aimZ=0;
    uint32_t    targetId = 0;

    bool isIdle()      const { return phase == Phase::Idle; }
    bool isCharging()  const { return phase == Phase::Charging; }
    bool isWindingUp() const { return phase == Phase::WindUp; }

    void reset() { *this = CastState{}; }

    static float calcCastTime(float mana, float baseTime, float timeScale) {
        float t = std::pow(mana / 100.f, 1.5f) * timeScale;
        return baseTime + t;
    }
    static float calcInterruptDC(float mana) { return mana * 0.3f; }
    static float calcPotency(float mana)     { return mana; }
};

class SpellManager {
public:
    void loadSpellsFromDir(const std::string& dir);

    float*           getFiringBudget()       { return _isFiring ? &_firingBudget : nullptr; }
    const CastState* getCurrentFiringState() { return _firingState; }
    ENetPeer*        getCurrentFiringPeer()  { return _firingPeer; }

    bool loadSpellSource(const std::string& name, const std::string& src);

    struct SpellMeta { float baseMana; float castTime; SpellElement element; };
    SpellMeta getSpellMeta(const std::string& name) const {
        auto it = _spells.find(name);
        if (it == _spells.end()) return {20.f, 0.5f, SpellElement::None};
        return {it->second.baseMana, it->second.baseTime, it->second.element};
    }

    struct FireEvent {
        ENetPeer*    peer;
        std::string  spellName;
        float        manaSpent;
        float        potency;
        float        aimX, aimY, aimZ;
        uint32_t     targetId;
        SpellElement element; // add
    };
    std::vector<FireEvent> update(float dt);

    bool beginCharge(ENetPeer* peer, const std::string& spellName,
                     float aimX, float aimY, float aimZ, uint32_t targetId,
                     const PlayerStats& stats, const Inventory& inv);
    void tickCharge(ENetPeer* peer, float dt, PlayerStats& stats);
    bool commitCast(ENetPeer* peer, PlayerStats& stats);
    void cancelCast(ENetPeer* peer);
    bool onDamageTaken(ENetPeer* peer, float damage, PlayerStats& stats);
    void onPlayerRemoved(ENetPeer* peer);

    bool  isOnCooldown(ENetPeer* peer, const std::string& spell) const;
    float getCooldown (ENetPeer* peer, const std::string& spell) const;

    void registerNative(const std::string& name, Aether::NativeFn fn);

    Aether::ExecResult triggerRune(const std::string& name,
                                   const std::string& triggerType,
                                   std::vector<Aether::Value> args = {});
    bool isRune(const std::string& name) const;

    const CastState* getCastState(ENetPeer* peer) const;
    bool             hasspell(const std::string& name) const;

private:
    std::unordered_map<std::string, SpellDef>   _spells;
    std::unordered_map<ENetPeer*, CastState>    _castStates;
    std::unordered_map<ENetPeer*,
        std::unordered_map<std::string, float>> _cooldowns;
    std::vector<std::pair<std::string, Aether::NativeFn>> _pendingNatives;

    float            _firingBudget = 0.f;
    bool             _isFiring     = false;
    const CastState* _firingState  = nullptr;
    ENetPeer*        _firingPeer   = nullptr;

    SpellDef*  findSpell(const std::string& name);
    CastState& stateFor(ENetPeer* peer);
    void       tickCooldowns(float dt);
    void       applyCooldown(ENetPeer* peer, const std::string& spell, float mana);
    bool       hasStaffEquipped(const Inventory& inv) const;

    static float calcCooldown(float mana) { return 0.5f + mana / 40.f; }
};
