#pragma once
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <unordered_map>
#include <unordered_set>
#include "chunk.h"
#include "camera.h"
#include "input.h"
#include "config.h"
#include "combat.h"

// ── ECS Components ────────────────────────────────────────────────────────────

struct CTransform { glm::vec3 pos{0.f}; };
struct CVelocity  { glm::vec3 vel{0.f}; };
struct CAABB      { glm::vec3 half{ Config::PLAYER_WIDTH  * 0.5f,
                                    Config::PLAYER_HEIGHT * 0.5f,
                                    Config::PLAYER_WIDTH  * 0.5f }; };
struct CGrounded  { bool grounded = false; };
struct CStamina {
    float current = 100.f, max = 100.f;
    float regenRate = 15.f, sprintCost = 20.f, jumpCost = 15.f;
    bool  depleted = false;
    float depleteCooldown = 0.f;
};

struct ChunkTriSoup { std::vector<glm::vec3> tris; };

// ── PlayerController ──────────────────────────────────────────────────────────

class CombatSystem; // forward decl

class PlayerController {
public:
    PlayerController(entt::registry& reg, Camera& cam);

    void addChunkMesh(const ChunkMesh& mesh);
    void removeChunk(ChunkCoord coord);
    void setSpawnPosition(glm::vec3 pos);

    // Pass combat system so player input can trigger attacks/dodge/parry
    void update(float dt, const Input& input, CombatSystem* combat = nullptr);

    glm::vec3    position()  const;
    entt::entity entity()    const { return _player; }
    bool         isSpawned() const { return _spawned; }

    float spawnProgress() const;

    const CStamina& stamina() const { return _reg.get<CStamina>(_player); }
    const CHealth&  health()  const { return _reg.get<CHealth>(_player); }
    const CAttack&  attack()  const { return _reg.get<CAttack>(_player); }
    const CParry&   parry()   const { return _reg.get<CParry>(_player); }
    const CDodge&   dodge()   const { return _reg.get<CDodge>(_player); }

private:
    entt::registry& _reg;
    Camera&         _cam;
    entt::entity    _player;

    std::unordered_map<ChunkCoord, ChunkTriSoup, ChunkCoordHash> _triSoups;

    bool      _spawned         = false;
    bool      _hasPendingSpawn = false;
    glm::vec3 _pendingSpawn    {0.f, 120.f, 0.f};

    std::unordered_set<ChunkCoord, ChunkCoordHash> _requiredChunks;

    void buildRequiredChunks(glm::vec3 pos);
    bool spawnChunksReady()  const;

    void resolveCollision(CTransform& tf, CVelocity& vel,
                          const CAABB& box, CGrounded& grounded);

    bool aabbTriTest(glm::vec3 mn, glm::vec3 mx,
                     glm::vec3 a, glm::vec3 b, glm::vec3 c,
                     glm::vec3& outMTV) const;
};