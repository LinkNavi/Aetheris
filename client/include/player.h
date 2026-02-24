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

// ── Per-chunk CPU triangle soup ───────────────────────────────────────────────

struct ChunkTriSoup {
    std::vector<glm::vec3> tris; // flat: every 3 = one triangle, world space
};

// ── Player controller ─────────────────────────────────────────────────────────

class PlayerController {
public:
    PlayerController(entt::registry& reg, Camera& cam);

    void addChunkMesh(const ChunkMesh& mesh);
    void removeChunk(ChunkCoord coord);
    void setSpawnPosition(glm::vec3 pos); // stores pending, applied at gate release
    void update(float dt, const Input& input);

    glm::vec3    position()  const;
    entt::entity entity()    const { return _player; }
    bool         isSpawned() const { return _spawned; }

private:
    entt::registry& _reg;
    Camera&         _cam;
    entt::entity    _player;

    std::unordered_map<ChunkCoord, ChunkTriSoup, ChunkCoordHash> _triSoups;

    // Spawn gate — hold player until enough chunks arrive, then teleport to surface
    bool      _spawned          = false;
    int       _chunksNeeded     = 27; // full 3x3x3 must be present
    bool      _hasPendingSpawn  = false;
    glm::vec3 _pendingSpawn     {0.f, 80.f, 0.f};

    void resolveCollision(CTransform& tf, CVelocity& vel,
                          const CAABB& box, CGrounded& grounded);

    bool aabbTriTest(glm::vec3 aabbMin, glm::vec3 aabbMax,
                     glm::vec3 a, glm::vec3 b, glm::vec3 c,
                     glm::vec3& outMTV) const;
};
