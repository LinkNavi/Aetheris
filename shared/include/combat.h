#pragma once
#include <glm/vec3.hpp>
#include <cstdint>

// ── Frame-data driven attack timing ──────────────────────────────────────────
// All times in seconds. Active window = when hitbox is live.
struct AttackData {
    float startup;   // time before hitbox activates
    float active;    // duration hitbox is live
    float recovery;  // time after active before next action
    float damage;
    float knockback;
    glm::vec3 hitboxOffset; // relative to attacker position
    glm::vec3 hitboxHalf;   // AABB half-extents of hitbox
};

// Sword move set
namespace SwordMoves {
    // Light attack
    inline constexpr AttackData LIGHT {
        .startup   = 0.15f,
        .active    = 0.10f,
        .recovery  = 0.30f,
        .damage    = 15.f,
        .knockback = 3.f,
        .hitboxOffset = {0.f, 0.f, -0.9f}, // in front of attacker
        .hitboxHalf   = {0.4f, 0.6f, 0.5f}
    };
    // Heavy attack
    inline constexpr AttackData HEAVY {
        .startup   = 0.30f,
        .active    = 0.15f,
        .recovery  = 0.55f,
        .damage    = 35.f,
        .knockback = 7.f,
        .hitboxOffset = {0.f, 0.f, -1.1f},
        .hitboxHalf   = {0.6f, 0.7f, 0.6f}
    };
}

// ── ECS Components ────────────────────────────────────────────────────────────

struct CHealth {
    float current = 100.f;
    float max     = 100.f;
    bool  dead    = false;
};

// Tracks current attack state
struct CAttack {
    enum class State { Idle, Startup, Active, Recovery };
    State state     = State::Idle;
    float timer     = 0.f;  // time remaining in current state
    const AttackData* data = nullptr;

    bool isIdle()     const { return state == State::Idle; }
    bool isActive()   const { return state == State::Active; }
    bool canAct()     const { return state == State::Idle; }
};

// Parry window
struct CParry {
    enum class State { Idle, Active, Cooldown };
    State state   = State::Idle;
    float timer   = 0.f;

    static constexpr float WINDOW   = 0.20f; // how long parry is active
    static constexpr float COOLDOWN = 0.50f;

    bool isActive() const { return state == State::Active; }
};

// Dodge roll with I-frames
struct CDodge {
    enum class State { Idle, Rolling, Cooldown };
    State     state     = State::Idle;
    float     timer     = 0.f;
    glm::vec3 dir       = {0.f, 0.f, 0.f};
    float     speed     = 12.f;

    static constexpr float DURATION  = 0.30f;
    static constexpr float IFRAMES   = 0.20f; // I-frame window inside roll duration
    static constexpr float COOLDOWN  = 0.50f;
    static constexpr float STAM_COST = 20.f;

    bool isRolling()   const { return state == State::Rolling; }
    bool hasIFrames()  const { return state == State::Rolling && timer > (DURATION - IFRAMES); }
    bool canDodge()    const { return state == State::Idle; }
};

// Tags an entity as invincible (I-frames, parry success, etc.)
struct CInvincible {
    float timer = 0.f; // remaining invincibility time
};

// Marks an entity as a damage source this frame
struct CHitThisFrame {
    glm::vec3 worldMin;
    glm::vec3 worldMax;
    float     damage;
    float     knockback;
    glm::vec3 knockDir;
    bool      fromPlayer; // so enemies don't hit each other (for now)
};

// Simple cube enemy (placeholder)
struct CEnemy {
    enum class AIState { Patrol, Aggro, Attack, Dead };
    AIState   ai          = AIState::Patrol;
    glm::vec3 patrolOrigin{0.f};
    float     aggroRange  = 12.f;
    float     attackRange = 1.8f;
    float     attackTimer = 0.f;
    float     attackCooldown = 1.5f;
    glm::vec3 knockbackVel{0.f};
};