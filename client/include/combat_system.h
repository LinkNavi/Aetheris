#pragma once
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <vector>
#include <unordered_map>
#include <cmath>
#include "combat.h"
#include "config.h"
#include "player.h"

// ── Helper ────────────────────────────────────────────────────────────────────
static inline bool aabbOverlap(glm::vec3 mnA, glm::vec3 mxA,
                                glm::vec3 mnB, glm::vec3 mxB) {
    return mnA.x <= mxB.x && mxA.x >= mnB.x &&
           mnA.y <= mxB.y && mxA.y >= mnB.y &&
           mnA.z <= mxB.z && mxA.z >= mnB.z;
}

class CombatSystem {
public:
    explicit CombatSystem(entt::registry& reg) : _reg(reg) {}

    // ── Player input interface ────────────────────────────────────────────────

    void playerLightAttack(entt::entity player, glm::vec3 facingDir) {
        startAttack(player, &SwordMoves::LIGHT, facingDir);
    }

    void playerHeavyAttack(entt::entity player, glm::vec3 facingDir) {
        auto& sta = _reg.get<CStamina>(player);
        if (sta.current < 25.f || sta.depleted) return;
        sta.current -= 25.f;
        startAttack(player, &SwordMoves::HEAVY, facingDir);
    }

    void playerParry(entt::entity player) {
        auto& atk = _reg.get<CAttack>(player);
        auto& par = _reg.get<CParry>(player);
        if (!atk.isIdle() || !par.isActive() && par.state != CParry::State::Idle) return;
        if (par.state != CParry::State::Idle) return;
        par.state = CParry::State::Active;
        par.timer = CParry::WINDOW;
    }

    void playerDodge(entt::entity player, glm::vec3 wishDir) {
        auto& dod = _reg.get<CDodge>(player);
        auto& sta = _reg.get<CStamina>(player);
        auto& atk = _reg.get<CAttack>(player);
        if (!dod.canDodge() || sta.depleted || sta.current < CDodge::STAM_COST) return;
        if (!atk.isIdle()) return;

        sta.current -= CDodge::STAM_COST;
        dod.state = CDodge::State::Rolling;
        dod.timer = CDodge::DURATION;
        float len = glm::length(wishDir);
        dod.dir   = (len > 0.001f) ? wishDir / len : glm::vec3{0.f, 0.f, -1.f};
    }

    // ── Per-frame update ──────────────────────────────────────────────────────

    void update(float dt, entt::entity playerEntity) {
        tickAttacks(dt, playerEntity);
        tickParry(dt, playerEntity);
        tickDodge(dt, playerEntity);
        tickInvincibility(dt);
        tickEnemyAI(dt, playerEntity);
        resolveHits(playerEntity);
        clearHits();
        tickEnemyKnockback(dt);
    }

    // Returns dodge velocity if rolling, else zero
    glm::vec3 getDodgeVelocity(entt::entity e) const {
        if (!_reg.all_of<CDodge>(e)) return {};
        const auto& dod = _reg.get<CDodge>(e);
        if (!dod.isRolling()) return {};
        return dod.dir * dod.speed;
    }

    bool isDodging(entt::entity e) const {
        if (!_reg.all_of<CDodge>(e)) return false;
        return _reg.get<CDodge>(e).isRolling();
    }

    // ── Enemy spawning ────────────────────────────────────────────────────────

    entt::entity spawnEnemy(glm::vec3 pos) {
        auto e = _reg.create();
        _reg.emplace<CTransform>(e, pos);
        _reg.emplace<CVelocity> (e);
        _reg.emplace<CAABB>     (e, glm::vec3{0.5f, 0.5f, 0.5f});
        _reg.emplace<CHealth>   (e, 60.f, 60.f);
        _reg.emplace<CAttack>   (e);
        _reg.emplace<CEnemy>    (e, CEnemy{.patrolOrigin = pos});
        return e;
    }

    // Access for renderer to draw enemy cubes
    void forEachEnemy(auto fn) const {
        _reg.view<CTransform, CEnemy, CHealth>().each(fn);
    }

private:
    entt::registry& _reg;

    // ── Attack ticking ────────────────────────────────────────────────────────
    void startAttack(entt::entity e, const AttackData* data, glm::vec3 facingDir) {
        auto& atk = _reg.get<CAttack>(e);
        if (!atk.isIdle()) return;
        atk.data  = data;
        atk.state = CAttack::State::Startup;
        atk.timer = data->startup;
        // Store facing in a scratch component so we know where to place hitbox
        _attackDir[e] = glm::normalize(glm::vec3{facingDir.x, 0.f, facingDir.z});
    }

    void tickAttacks(float dt, entt::entity playerEntity) {
        _reg.view<CAttack, CTransform>().each([&](entt::entity e, CAttack& atk, CTransform& tf) {
            if (atk.isIdle()) return;
            atk.timer -= dt;
            if (atk.timer > 0.f) return;

            switch (atk.state) {
            case CAttack::State::Startup:
                atk.state = CAttack::State::Active;
                atk.timer = atk.data->active;
                emitHitbox(e, tf, atk, e == playerEntity);
                break;
            case CAttack::State::Active:
                atk.state = CAttack::State::Recovery;
                atk.timer = atk.data->recovery;
                break;
            case CAttack::State::Recovery:
                atk.state = CAttack::State::Idle;
                atk.timer = 0.f;
                atk.data  = nullptr;
                break;
            default: break;
            }
        });
    }

    void emitHitbox(entt::entity attacker, CTransform tf,
                    const CAttack& atk, bool fromPlayer) {
        glm::vec3 facing{0.f, 0.f, -1.f};
        auto it = _attackDir.find(attacker);
        if (it != _attackDir.end()) facing = it->second;

        // Rotate hitbox offset by facing (simplified: only yaw)
        float yaw = std::atan2(facing.x, facing.z);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        glm::vec3 off = atk.data->hitboxOffset;
        glm::vec3 rotOff{
            off.x * cy + off.z * sy,
            off.y,
            -off.x * sy + off.z * cy
        };

        glm::vec3 centre = tf.pos + rotOff;
        auto hit = _reg.create();
        _reg.emplace<CHitThisFrame>(hit,
            centre - atk.data->hitboxHalf,
            centre + atk.data->hitboxHalf,
            atk.data->damage,
            atk.data->knockback,
            facing,
            fromPlayer
        );
        _pendingHits.push_back(hit);
    }

    // ── Parry ticking ─────────────────────────────────────────────────────────
    void tickParry(float dt, entt::entity player) {
        auto& par = _reg.get<CParry>(player);
        if (par.state == CParry::State::Idle) return;
        par.timer -= dt;
        if (par.timer > 0.f) return;
        if (par.state == CParry::State::Active) {
            par.state = CParry::State::Cooldown;
            par.timer = CParry::COOLDOWN;
        } else {
            par.state = CParry::State::Idle;
        }
    }

    // ── Dodge ticking ─────────────────────────────────────────────────────────
    void tickDodge(float dt, entt::entity player) {
        auto& dod = _reg.get<CDodge>(player);
        if (dod.state == CDodge::State::Idle) return;
        dod.timer -= dt;
        if (dod.timer > 0.f) return;
        if (dod.state == CDodge::State::Rolling) {
            dod.state = CDodge::State::Cooldown;
            dod.timer = CDodge::COOLDOWN;
        } else {
            dod.state = CDodge::State::Idle;
        }
    }

    // ── Invincibility ticking ─────────────────────────────────────────────────
    void tickInvincibility(float dt) {
        auto view = _reg.view<CInvincible>();
        for (auto e : view) {
            auto& inv = view.get<CInvincible>(e);
            inv.timer -= dt;
            if (inv.timer <= 0.f) _reg.remove<CInvincible>(e);
        }
    }

    // ── Hit resolution ────────────────────────────────────────────────────────
    void resolveHits(entt::entity playerEntity) {
        for (auto hitEnt : _pendingHits) {
            if (!_reg.valid(hitEnt)) continue;
            auto& h = _reg.get<CHitThisFrame>(hitEnt);

            if (h.fromPlayer) {
                // Player hit → check enemies
                _reg.view<CTransform, CAABB, CHealth, CEnemy>().each(
                [&](entt::entity e, CTransform& tf, CAABB& box, CHealth& hp, CEnemy& en) {
                    if (hp.dead) return;
                    glm::vec3 mn = tf.pos - box.half;
                    glm::vec3 mx = tf.pos + box.half;
                    if (!aabbOverlap(h.worldMin, h.worldMax, mn, mx)) return;

                    hp.current -= h.damage;
                    en.knockbackVel = h.knockDir * h.knockback;
                    if (hp.current <= 0.f) {
                        hp.current = 0.f;
                        hp.dead    = true;
                        en.ai      = CEnemy::AIState::Dead;
                    }
                });
            } else {
                // Enemy hit → check player (if not I-framing or parrying)
                if (!_reg.valid(playerEntity)) return;
                auto& pHP  = _reg.get<CHealth>(playerEntity);
                auto& pTF  = _reg.get<CTransform>(playerEntity);
                auto& pBox = _reg.get<CAABB>(playerEntity);
                auto& pPar = _reg.get<CParry>(playerEntity);
                auto& pDod = _reg.get<CDodge>(playerEntity);

                if (pHP.dead) return;
                if (_reg.all_of<CInvincible>(playerEntity)) return;
                if (pDod.hasIFrames()) return;

                glm::vec3 mn = pTF.pos - pBox.half;
                glm::vec3 mx = pTF.pos + pBox.half;
                if (!aabbOverlap(h.worldMin, h.worldMax, mn, mx)) return;

                if (pPar.isActive()) {
                    // Successful parry — grant brief invincibility + stagger attacker
                    pPar.state = CParry::State::Cooldown;
                    pPar.timer = CParry::COOLDOWN;
                    _reg.emplace_or_replace<CInvincible>(playerEntity, 0.5f);
                    // TODO: stagger the attacking enemy
                    return;
                }

                pHP.current -= h.damage;
                _reg.emplace_or_replace<CInvincible>(playerEntity, 0.3f); // brief iframes on hit
                if (pHP.current <= 0.f) {
                    pHP.current = 0.f;
                    pHP.dead    = true;
                }
            }
        }
    }

    void clearHits() {
        for (auto e : _pendingHits)
            if (_reg.valid(e)) _reg.destroy(e);
        _pendingHits.clear();
    }

    // ── Enemy AI ──────────────────────────────────────────────────────────────
    void tickEnemyAI(float dt, entt::entity playerEntity) {
        if (!_reg.valid(playerEntity)) return;
        auto& pTF = _reg.get<CTransform>(playerEntity);
        auto& pHP = _reg.get<CHealth>(playerEntity);

        _reg.view<CTransform, CEnemy, CAttack, CHealth>().each(
        [&](entt::entity e, CTransform& tf, CEnemy& en, CAttack& atk, CHealth& hp) {
            if (hp.dead) return;

            float dist = glm::length(pTF.pos - tf.pos);

            switch (en.ai) {
            case CEnemy::AIState::Patrol:
                if (!pHP.dead && dist < en.aggroRange)
                    en.ai = CEnemy::AIState::Aggro;
                break;

            case CEnemy::AIState::Aggro: {
                if (dist > en.aggroRange * 1.5f) { en.ai = CEnemy::AIState::Patrol; break; }
                // Move toward player
                glm::vec3 dir = pTF.pos - tf.pos;
                float len = glm::length(dir);
                if (len > 0.01f) {
                    tf.pos += (dir / len) * 3.5f * dt; // enemy walk speed
                }
                if (dist < en.attackRange) en.ai = CEnemy::AIState::Attack;
                break;
            }

            case CEnemy::AIState::Attack:
                if (dist > en.attackRange * 1.5f) { en.ai = CEnemy::AIState::Aggro; break; }
                en.attackTimer -= dt;
                if (en.attackTimer <= 0.f && atk.isIdle()) {
                    en.attackTimer = en.attackCooldown;
                    glm::vec3 dir = glm::normalize(pTF.pos - tf.pos);
                    _attackDir[e] = dir;
                    atk.data  = &SwordMoves::LIGHT;
                    atk.state = CAttack::State::Startup;
                    atk.timer = SwordMoves::LIGHT.startup;
                    // Emit hitbox when active fires in tickAttacks
                }
                break;

            case CEnemy::AIState::Dead:
                break;
            }
        });
    }

    void tickEnemyKnockback(float dt) {
        _reg.view<CTransform, CEnemy>().each([&](CTransform& tf, CEnemy& en) {
            if (glm::length(en.knockbackVel) < 0.01f) return;
            tf.pos += en.knockbackVel * dt;
            en.knockbackVel *= std::max(0.f, 1.f - 10.f * dt); // friction
        });
    }

    std::unordered_map<entt::entity, glm::vec3> _attackDir;
    std::vector<entt::entity> _pendingHits;
};