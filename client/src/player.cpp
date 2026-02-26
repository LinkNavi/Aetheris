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

// ── PlayerController ──────────────────────────────────────────────────────────

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

void PlayerController::resolveCollision(CTransform& tf, CVelocity& vel,
                                         const CAABB& box, CGrounded& grounded) {
    glm::vec3 half = box.half;
    int sz = ChunkData::SIZE;
    int cx = (int)std::floor(tf.pos.x / sz);
    int cy = (int)std::floor(tf.pos.y / sz);
    int cz = (int)std::floor(tf.pos.z / sz);

    grounded.grounded = false;

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
                tf.pos += mtv;
                mn = tf.pos - half;
                mx = tf.pos + half;
                float vDot = glm::dot(vel.vel, glm::normalize(mtv));
                if (vDot < 0.f) vel.vel -= glm::normalize(mtv) * vDot;
                if (glm::normalize(mtv).y > 0.5f) grounded.grounded = true;
            }
        }
    }
}

static glm::vec3 accelerate(glm::vec3 vel, glm::vec3 dir, float speed, float accel, float dt) {
    float cur  = glm::dot(vel, dir);
    float add  = speed - cur;
    if (add <= 0.f) return vel;
    return vel + dir * std::min(accel * speed * dt, add);
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

    // Dead — don't process input
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

    glm::vec3 fwd = _cam.forward(); fwd.y = 0.f;
    float fwdLen = glm::length(fwd);
    if (fwdLen > 0.001f) fwd /= fwdLen;
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3{0,1,0}));

    glm::vec3 wishDir{0.f};
    if (input.key(GLFW_KEY_W)) wishDir += fwd;
    if (input.key(GLFW_KEY_S)) wishDir -= fwd;
    if (input.key(GLFW_KEY_D)) wishDir += right;
    if (input.key(GLFW_KEY_A)) wishDir -= right;
    float wishLen = glm::length(wishDir);
    if (wishLen > 0.001f) wishDir /= wishLen;

    // ── Combat input ──────────────────────────────────────────────────────────
    if (combat) {
        // LMB = light attack
        if (input.keyDown(GLFW_MOUSE_BUTTON_LEFT + 400)) // placeholder — wire to mouse later
            combat->playerLightAttack(_player, _cam.forward());

        // Left click: GLFW mouse buttons come through key callbacks with offset
        // For now use keyboard: F = light, G = heavy, Q = parry, Space+dir = dodge
        if (input.keyDown(GLFW_KEY_F))
            combat->playerLightAttack(_player, _cam.forward());
        if (input.keyDown(GLFW_KEY_G))
            combat->playerHeavyAttack(_player, _cam.forward());
        if (input.keyDown(GLFW_KEY_Q))
            combat->playerParry(_player);

        // Dodge: Left Ctrl + movement direction
        if (input.keyDown(GLFW_KEY_LEFT_CONTROL) && wishLen > 0.001f)
            combat->playerDodge(_player, wishDir);
    }

    // ── Stamina ───────────────────────────────────────────────────────────────
    if (sta.depleted) {
        sta.depleteCooldown -= dt;
        if (sta.depleteCooldown <= 0.f) sta.depleted = false;
    }

    // Don't allow sprint during attack or dodge
    auto& atk = _reg.get<CAttack>(_player);
    auto& dod = _reg.get<CDodge>(_player);
    bool sprinting = input.key(GLFW_KEY_LEFT_SHIFT) && !sta.depleted
                     && sta.current > 0.f && atk.isIdle() && !dod.isRolling();

    if (sprinting && wishLen > 0.001f) {
        sta.current -= sta.sprintCost * dt;
        if (sta.current <= 0.f) {
            sta.current = 0.f; sta.depleted = true;
            sta.depleteCooldown = 1.5f; sprinting = false;
        }
    } else if (!sta.depleted) {
        sta.current = std::min(sta.current + sta.regenRate * dt, sta.max);
    }

    float wishSpeed = wishLen > 0.001f
        ? Config::WALK_SPEED * (sprinting ? Config::SPRINT_MULT : 1.f)
        : 0.f;

    // Slow down during attack recovery
    if (!atk.isIdle()) wishSpeed *= 0.3f;

    glm::vec3 hVel{vel.vel.x, 0.f, vel.vel.z};
    float     yVel = vel.vel.y;

    // ── Dodge overrides horizontal velocity ───────────────────────────────────
    if (combat && dod.isRolling()) {
        glm::vec3 dv = combat->getDodgeVelocity(_player);
        hVel = {dv.x, 0.f, dv.z};
    } else if (gr.grounded) {
        float speed = glm::length(hVel);
        if (speed > 0.001f) {
            float drop = speed * Config::FRICTION * dt;
            hVel *= std::max(speed - drop, 0.f) / speed;
        }
        hVel = accelerate(hVel, wishDir, wishSpeed, Config::GROUND_ACCEL, dt);
        if (yVel < 0.f) yVel = 0.f;
        if (input.key(GLFW_KEY_SPACE) && !sta.depleted && sta.current >= sta.jumpCost
            && atk.isIdle() && !dod.isRolling()) {
            sta.current -= sta.jumpCost;
            yVel = Config::JUMP_VEL;
            gr.grounded = false;
        }
    } else {
        hVel = accelerate(hVel, wishDir, wishSpeed, Config::AIR_ACCEL, dt);
        yVel += Config::GRAVITY * dt;
    }

    vel.vel = {hVel.x, yVel, hVel.z};

    const float subDt = dt / 4.f;
    for (int s = 0; s < 4; s++) {
        tf.pos += vel.vel * subDt;
        resolveCollision(tf, vel, box, gr);
    }

    _cam.position = tf.pos + glm::vec3{0.f, box.half.y * 0.85f, 0.f};
}