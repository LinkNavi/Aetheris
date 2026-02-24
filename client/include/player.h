#pragma once
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <unordered_map>
#include "chunk.h"
#include "camera.h"
#include "input.h"
#include "config.h"

// ── ECS Components ────────────────────────────────────────────────────────────

struct CTransform {
    glm::vec3 pos{0.f};
};

struct CVelocity {
    glm::vec3 vel{0.f};
};

struct CAABB {
    glm::vec3 half{ Config::PLAYER_WIDTH  * 0.5f,
                    Config::PLAYER_HEIGHT * 0.5f,
                    Config::PLAYER_WIDTH  * 0.5f };
};

struct CGrounded {
    bool grounded = false;
};

struct CStamina {
    float current         = 100.f;
    float max             = 100.f;
    float regenRate       = 15.f;  // per second
    float sprintCost      = 20.f;  // per second
    float jumpCost        = 15.f;  // flat per jump
    bool  depleted        = false;
    float depleteCooldown = 0.f;
};

// ── Per-chunk CPU triangle soup ───────────────────────────────────────────────

struct ChunkTriSoup {
    std::vector<glm::vec3> tris;
};

// ── Player controller ─────────────────────────────────────────────────────────

class PlayerController {
public:
    PlayerController(entt::registry& reg, Camera& cam);

    void addChunkMesh(const ChunkMesh& mesh);
    void removeChunk(ChunkCoord coord);
    void setSpawnPosition(glm::vec3 pos);
    void update(float dt, const Input& input);

    glm::vec3    position()  const;
    entt::entity entity()    const { return _player; }
    bool         isSpawned() const { return _spawned; }

    // Expose stamina for HUD
    const CStamina& stamina() const { return _reg.get<CStamina>(_player); }

private:
    entt::registry& _reg;
    Camera&         _cam;
    entt::entity    _player;

    std::unordered_map<ChunkCoord, ChunkTriSoup, ChunkCoordHash> _triSoups;

    bool      _spawned         = false;
    int       _chunksNeeded    = 27;
    bool      _hasPendingSpawn = false;
    glm::vec3 _pendingSpawn    {0.f, 80.f, 0.f};

    void resolveCollision(CTransform& tf, CVelocity& vel,
                          const CAABB& box, CGrounded& grounded);

    bool aabbTriTest(glm::vec3 aabbMin, glm::vec3 aabbMax,
                     glm::vec3 a, glm::vec3 b, glm::vec3 c,
                     glm::vec3& outMTV) const;
};
