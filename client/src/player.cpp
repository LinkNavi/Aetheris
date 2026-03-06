#include "player.h"
#include "../include/combat_system.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

// ── SAT helpers ───────────────────────────────────────────────────────────────

static float projectAABB(glm::vec3 n, glm::vec3 half) {
    return std::abs(n.x)*half.x + std::abs(n.y)*half.y + std::abs(n.z)*half.z;
}

static bool axisTest(glm::vec3 axis, glm::vec3 centre, glm::vec3 half,
                     glm::vec3 a, glm::vec3 b, glm::vec3 c,
                     float& depth, glm::vec3& mtvAxis) {
    float len2 = glm::dot(axis, axis);
    if (len2 < 1e-8f) return true;
    glm::vec3 n = axis / std::sqrt(len2);
    float pa = glm::dot(n, a - centre), pb = glm::dot(n, b - centre), pc = glm::dot(n, c - centre);
    float lo = std::min({pa,pb,pc}), hi = std::max({pa,pb,pc});
    float r  = projectAABB(n, half);
    if (lo > r || hi < -r) return false;
    float overlap = std::min(r - lo, hi + r);
    if (overlap < depth) { depth = overlap; mtvAxis = n; }
    return true;
}

bool PlayerController::aabbTriTest(glm::vec3 mn, glm::vec3 mx,
                                    glm::vec3 a, glm::vec3 b, glm::vec3 c,
                                    glm::vec3& outMTV) const {
    glm::vec3 half   = (mx - mn) * 0.5f;
    glm::vec3 centre = (mn + mx) * 0.5f;
    glm::vec3 ab = b-a, bc = c-b, ca = a-c;
    float     depth  = 1e9f;
    glm::vec3 mtvAxis{0,1,0};
    glm::vec3 axes[3] = {{1,0,0},{0,1,0},{0,0,1}};

    for (auto& ax : axes)
        if (!axisTest(ax, centre, half, a, b, c, depth, mtvAxis)) return false;
    if (!axisTest(glm::cross(ab, c-a), centre, half, a, b, c, depth, mtvAxis)) return false;
    for (auto& e : {ab, bc, ca})
        for (auto& ax : axes)
            if (!axisTest(glm::cross(e, ax), centre, half, a, b, c, depth, mtvAxis)) return false;

    if (glm::dot(mtvAxis, a - centre) > 0) mtvAxis = -mtvAxis;
    outMTV = mtvAxis * depth;
    return true;
}

// ── Möller–Trumbore ray-triangle intersection ─────────────────────────────────
bool PlayerController::rayTriTest(glm::vec3 orig, glm::vec3 dir, float maxDist,
                                   glm::vec3 a, glm::vec3 b, glm::vec3 c,
                                   float& outT) const {
    constexpr float EPS = 1e-7f;
    glm::vec3 ab = b - a, ac = c - a;
    glm::vec3 h  = glm::cross(dir, ac);
    float det    = glm::dot(ab, h);
    if (std::abs(det) < EPS) return false;
    float invDet = 1.f / det;
    glm::vec3 s  = orig - a;
    float u      = glm::dot(s, h) * invDet;
    if (u < 0.f || u > 1.f) return false;
    glm::vec3 q  = glm::cross(s, ab);
    float v      = glm::dot(dir, q) * invDet;
    if (v < 0.f || u + v > 1.f) return false;
    float t      = glm::dot(ac, q) * invDet;
    if (t < EPS || t > maxDist) return false;
    outT = t;
    return true;
}

// ── Raycast ground detection ──────────────────────────────────────────────────
// Fires a ray straight down from feet. Returns true + hit Y if terrain found.
bool PlayerController::raycastGround(const CTransform& tf, const CAABB& box,
                                      float& outHitY) const {
    // Start rays slightly ABOVE feet so we don't clip through after collision resolve
    constexpr float RAY_START  = 0.05f; // above feet
    constexpr float RAY_LEN    = 0.55f; // total downward distance to check
    constexpr float FOOT_INSET = 0.08f;

    float footY   = tf.pos.y - box.half.y;
    float origY   = footY + RAY_START;
    float maxDist = RAY_LEN;

    float inset = box.half.x - FOOT_INSET;
    // 5 rays: centre + 4 corners
    glm::vec3 origins[5] = {
        {tf.pos.x,         origY, tf.pos.z        },
        {tf.pos.x + inset, origY, tf.pos.z + inset},
        {tf.pos.x - inset, origY, tf.pos.z + inset},
        {tf.pos.x + inset, origY, tf.pos.z - inset},
        {tf.pos.x - inset, origY, tf.pos.z - inset},
    };
    glm::vec3 dir{0.f, -1.f, 0.f};

    int sz = ChunkData::SIZE;
    int cx = (int)std::floor(tf.pos.x / sz);
    int cy = (int)std::floor(tf.pos.y / sz);
    int cz = (int)std::floor(tf.pos.z / sz);

    float bestT = maxDist + 1.f;
    bool  hit   = false;

    for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dz = -1; dz <= 1; dz++) {
        auto it = _triSoups.find({cx+dx, cy+dy, cz+dz});
        if (it == _triSoups.end()) continue;
        const auto& tris = it->second.tris;
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            // Accept triangles whose normal has any upward component
            // (handles marching cubes winding inconsistencies)
            glm::vec3 edge1 = tris[i+1] - tris[i];
            glm::vec3 edge2 = tris[i+2] - tris[i];
            glm::vec3 n = glm::cross(edge1, edge2);
            // Skip purely vertical or downward-facing tris
            // Use a loose threshold — 0.1 catches even steep slopes
            if (n.y < 0.1f) continue;

            for (auto& orig : origins) {
                float t;
                if (rayTriTest(orig, dir, maxDist, tris[i], tris[i+1], tris[i+2], t)) {
                    if (t < bestT) {
                        bestT   = t;
                        // Hit Y = origin Y minus distance traveled
                        outHitY = orig.y - t;
                        hit     = true;
                    }
                }
            }
        }
    }
    return hit;
}// ── PlayerController ──────────────────────────────────────────────────────────

PlayerController::PlayerController(entt::registry& reg, Camera& cam)
    : _reg(reg), _cam(cam)
{
    _player = reg.create();
    reg.emplace<CTransform>(_player, glm::vec3{0.f, 80.f, 0.f});
    reg.emplace<CVelocity> (_player);
    reg.emplace<CAABB>     (_player);
    reg.emplace<CGrounded> (_player);
    reg.emplace<CStamina>  (_player);
    reg.emplace<CHealth>   (_player);
    reg.emplace<CAttack>   (_player);
    reg.emplace<CParry>    (_player);
    reg.emplace<CDodge>    (_player);
    reg.emplace<CInventory>(_player);
}

void PlayerController::addChunkMesh(const ChunkMesh& mesh) {
    if (mesh.vertices.empty()) return;
    ChunkTriSoup soup;
    int sz = ChunkData::SIZE;
    glm::vec3 offset{
        (float)(mesh.coord.x * sz),
        (float)(mesh.coord.y * sz),
        (float)(mesh.coord.z * sz)
    };
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        soup.tris.push_back(mesh.vertices[mesh.indices[i  ]].pos + offset);
        soup.tris.push_back(mesh.vertices[mesh.indices[i+1]].pos + offset);
        soup.tris.push_back(mesh.vertices[mesh.indices[i+2]].pos + offset);
    }
    _triSoups[mesh.coord] = std::move(soup);
}

void PlayerController::removeChunk(ChunkCoord coord) {
    _triSoups.erase(coord);
}

void PlayerController::setSpawnPosition(glm::vec3 pos) {
    _pendingSpawn    = pos;
    _hasPendingSpawn = true;
    _spawned         = false;
    _triSoups.clear();
    _smoothVel = {0.f, 0.f, 0.f};
    buildRequiredChunks(pos);
}

void PlayerController::buildRequiredChunks(glm::vec3 pos) {
    _requiredChunks.clear();
    int sz = ChunkData::SIZE;
    int cx = (int)std::floor(pos.x / sz);
    int cy = (int)std::floor(pos.y / sz);
    int cz = (int)std::floor(pos.z / sz);
    for (int dx = -1; dx <= 1; dx++)
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
        _requiredChunks.insert({cx+dx, cy+dy, cz+dz});
}

bool PlayerController::spawnChunksReady() const {
    if (!_hasPendingSpawn) return false;
    int N = ChunkData::SIZE;
    ChunkCoord atSpawn   { (int)std::floor(_pendingSpawn.x / N),
                           (int)std::floor(_pendingSpawn.y / N),
                           (int)std::floor(_pendingSpawn.z / N) };
    ChunkCoord belowSpawn{ atSpawn.x, atSpawn.y - 1, atSpawn.z };
    return _triSoups.count(atSpawn) && _triSoups.count(belowSpawn);
}

float PlayerController::spawnProgress() const {
    if (_spawned || _requiredChunks.empty()) return _spawned ? 1.f : 0.f;
    int have = 0;
    for (const auto& cc : _requiredChunks)
        if (_triSoups.count(cc)) have++;
    return (float)have / (float)_requiredChunks.size();
}

glm::vec3 PlayerController::position() const {
    return _reg.get<CTransform>(_player).pos;
}

// ── resolveCollision ──────────────────────────────────────────────────────────
void PlayerController::resolveCollision(CTransform& tf, CVelocity& vel,
                                         const CAABB& box, CGrounded& /*unused*/) {
    glm::vec3 half = box.half;
    int sz = ChunkData::SIZE;
    int cx = (int)std::floor(tf.pos.x / sz);
    int cy = (int)std::floor(tf.pos.y / sz);
    int cz = (int)std::floor(tf.pos.z / sz);

    for (int iter = 0; iter < 4; iter++) {
        glm::vec3 mn = tf.pos - half;
        glm::vec3 mx = tf.pos + half;

        for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) {
            auto it = _triSoups.find({cx+dx, cy+dy, cz+dz});
            if (it == _triSoups.end()) continue;
            for (size_t i = 0; i + 2 < it->second.tris.size(); i += 3) {
                glm::vec3 mtv;
                if (!aabbTriTest(mn, mx,
                        it->second.tris[i], it->second.tris[i+1], it->second.tris[i+2],
                        mtv)) continue;

                glm::vec3 mtvN = glm::normalize(mtv);
                if (std::abs(mtvN.y) > 0.7f) {
                    mtv  = glm::vec3(0.f, mtv.y, 0.f);
                    mtvN = glm::vec3(0.f, mtvN.y < 0.f ? -1.f : 1.f, 0.f);
                }

                tf.pos += mtv;
                mn = tf.pos - half;
                mx = tf.pos + half;

                float vDot = glm::dot(vel.vel, mtvN);
                if (vDot < 0.f) vel.vel -= mtvN * vDot;
            }
        }
    }
}

void PlayerController::update(float dt, const Input& input, CombatSystem* combat) {
    // ── Spawn gate ────────────────────────────────────────────────────────────
    if (!_spawned) {
        if (spawnChunksReady()) {
            _reg.get<CTransform>(_player).pos = _pendingSpawn;
            _reg.get<CVelocity> (_player).vel = {0.f, 0.f, 0.f};
            _hasPendingSpawn = false;
            _spawned         = true;
        } else {
            _cam.applyMouse(input.mouseDelta());
            return;
        }
    }

    auto& tf  = _reg.get<CTransform>(_player);
    auto& vel = _reg.get<CVelocity> (_player);
    auto& box = _reg.get<CAABB>     (_player);
    auto& gr  = _reg.get<CGrounded> (_player);
    auto& sta = _reg.get<CStamina>  (_player);
    auto& hp  = _reg.get<CHealth>   (_player);

    if (hp.dead) {
        _cam.applyMouse(input.mouseDelta());
        return;
    }

    // ── Chunk unload ──────────────────────────────────────────────────────────
    {
        int cx = (int)std::floor(tf.pos.x / ChunkData::SIZE);
        int cy = (int)std::floor(tf.pos.y / ChunkData::SIZE);
        int cz = (int)std::floor(tf.pos.z / ChunkData::SIZE);
        for (auto it = _triSoups.begin(); it != _triSoups.end(); ) {
            const auto& cc = it->first;
            if (std::abs(cc.x - cx) > Config::CHUNK_RADIUS_XZ + 1 ||
                std::abs(cc.y - cy) > Config::CHUNK_RADIUS_Y  + 1 ||
                std::abs(cc.z - cz) > Config::CHUNK_RADIUS_XZ + 1)
                it = _triSoups.erase(it);
            else
                ++it;
        }
    }

    _cam.applyMouse(input.mouseDelta());

    // ── Wish direction ────────────────────────────────────────────────────────
    glm::vec3 fwd = _cam.forward(); fwd.y = 0.f;
    float fwdLen = glm::length(fwd);
    if (fwdLen > 0.001f) fwd /= fwdLen;
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3{0,1,0}));

    glm::vec3 wishDir{0.f};
    bool movingFwd  = input.key(GLFW_KEY_W);
    bool movingBack = input.key(GLFW_KEY_S);
    bool movingR    = input.key(GLFW_KEY_D);
    bool movingL    = input.key(GLFW_KEY_A);

    if (movingFwd)  wishDir += fwd;
    if (movingBack) wishDir -= fwd;
    if (movingR)    wishDir += right;
    if (movingL)    wishDir -= right;
    float wishLen = glm::length(wishDir);
    if (wishLen > 0.001f) wishDir /= wishLen;

    // ── Combat input ──────────────────────────────────────────────────────────
    auto& atk = _reg.get<CAttack>(_player);
    auto& dod = _reg.get<CDodge> (_player);

    if (combat) {
        if (input.keyDown(GLFW_KEY_F))
            combat->playerLightAttack(_player, _cam.forward());
        if (input.keyDown(GLFW_KEY_G))
            combat->playerHeavyAttack(_player, _cam.forward());
        if (input.keyDown(GLFW_KEY_Q))
            combat->playerParry(_player);
        if (input.keyDown(GLFW_KEY_LEFT_CONTROL) && wishLen > 0.001f)
            combat->playerDodge(_player, wishDir);
    }

    // ── Stamina ───────────────────────────────────────────────────────────────
    if (sta.depleted) {
        sta.depleteCooldown -= dt;
        if (sta.depleteCooldown <= 0.f) sta.depleted = false;
    }

    bool sprinting = input.key(GLFW_KEY_LEFT_SHIFT)
                     && !sta.depleted
                     && sta.current > 0.f
                     && atk.isIdle()
                     && !dod.isRolling()
                     && wishLen > 0.001f;

    if (sprinting) {
        sta.current -= sta.sprintCost * dt;
        if (sta.current <= 0.f) {
            sta.current = 0.f;
            sta.depleted = true;
            sta.depleteCooldown = 1.5f;
            sprinting = false;
        }
    } else if (!sta.depleted) {
        sta.current = std::min(sta.current + sta.regenRate * dt, sta.max);
    }

    // ── Target speed (Skyrim-style: back is slower, strafe slightly slower) ──
    float baseSpeed = Config::WALK_SPEED * (sprinting ? Config::SPRINT_MULT : 1.f);
    if (!atk.isIdle()) baseSpeed *= 0.3f;

    // Directional speed multipliers (Skyrim feel)
    float speedMult = 1.f;
    if (movingBack && !movingFwd)          speedMult = 0.65f; // backpedal slower
    else if ((movingL || movingR) &&
             !movingFwd && !movingBack)    speedMult = 0.85f; // pure strafe slightly slower
    float targetSpeed = baseSpeed * speedMult;

    // ── Gravity ───────────────────────────────────────────────────────────────
    constexpr float MAX_FALL = 60.f;
    if (!gr.grounded) {
        vel.vel.y += Config::GRAVITY * dt;
        if (vel.vel.y < -MAX_FALL) vel.vel.y = -MAX_FALL;
    }

    // ── Sub-step integrate + collide ──────────────────────────────────────────
    const float subDt = dt / 4.f;
    CGrounded unused;
    for (int s = 0; s < 4; s++) {
        tf.pos += vel.vel * subDt;
        resolveCollision(tf, vel, box, unused);
    }

    // ── Raycast ground detection ──────────────────────────────────────────────
    float hitY  = 0.f;
    bool onGround = raycastGround(tf, box, hitY);

    if (onGround && vel.vel.y <= 0.f) {
        // Snap feet to surface
        float targetFootY = hitY + box.half.y;
        // Only snap if we're close (avoids teleporting through geometry)
        if (std::abs(tf.pos.y - targetFootY) < 0.35f) {
            tf.pos.y = targetFootY;
        }
        vel.vel.y = 0.f;
        gr.grounded = true;
    } else {
        gr.grounded = false;
    }

    // ── Horizontal movement (Skyrim-style smooth accel/decel) ─────────────────
    if (dod.isRolling() && combat) {
        glm::vec3 dv = combat->getDodgeVelocity(_player);
        _smoothVel.x = dv.x;
        _smoothVel.z = dv.z;
    } else if (gr.grounded) {
        glm::vec3 targetHoriz = wishDir * (wishLen > 0.001f ? targetSpeed : 0.f);

        // Skyrim uses fast accel, moderate decel — feels weighty but responsive
        float accel  = (wishLen > 0.001f) ? Config::GROUND_ACCEL : Config::FRICTION;
        float blend  = std::min(accel * dt, 1.f);
        _smoothVel.x += (targetHoriz.x - _smoothVel.x) * blend;
        _smoothVel.z += (targetHoriz.z - _smoothVel.z) * blend;

        vel.vel.x = _smoothVel.x;
        vel.vel.z = _smoothVel.z;

        // Jump
        if (input.keyDown(GLFW_KEY_SPACE)
            && !sta.depleted
            && sta.current >= sta.jumpCost
            && atk.isIdle()
            && !dod.isRolling())
        {
            sta.current -= sta.jumpCost;
            vel.vel.y   = Config::JUMP_VEL;
            gr.grounded = false;
        }
    } else {
        // Air: carry momentum, minimal steering (Skyrim has very little air control)
        glm::vec3 targetHoriz = wishDir * (wishLen > 0.001f ? targetSpeed : 0.f);
        float blend = std::min(Config::AIR_ACCEL * dt, 1.f);
        _smoothVel.x += (targetHoriz.x - _smoothVel.x) * blend;
        _smoothVel.z += (targetHoriz.z - _smoothVel.z) * blend;
        vel.vel.x = _smoothVel.x;
        vel.vel.z = _smoothVel.z;
    }

    // ── Head bob ──────────────────────────────────────────────────────────────
    if (gr.grounded && wishLen > 0.001f) {
        static float bobTime = 0.f;
        float speed = glm::length(glm::vec3{vel.vel.x, 0.f, vel.vel.z});
        bobTime += dt * speed * 0.4f;
        float bobY = std::sin(bobTime * 2.f) * 0.04f;
        float bobX = std::sin(bobTime) * 0.02f;
        _cam.position.y += bobY;
        _cam.position += _cam.right() * bobX;
    }

    _cam.position = tf.pos + glm::vec3{0.f, box.half.y * 0.85f, 0.f};
}
