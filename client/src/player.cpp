#include "player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// ── SAT helpers ───────────────────────────────────────────────────────────────

static float projectAABB(glm::vec3 axis, glm::vec3 half) {
    return std::abs(axis.x)*half.x + std::abs(axis.y)*half.y + std::abs(axis.z)*half.z;
}

static bool axisTest(glm::vec3 axis, glm::vec3 centre, glm::vec3 half,
                     glm::vec3 a, glm::vec3 b, glm::vec3 c,
                     float& depth, glm::vec3& mtvAxis) {
    float len2 = glm::dot(axis, axis);
    if (len2 < 1e-8f) return true;
    glm::vec3 n = axis / std::sqrt(len2);
    float pa = glm::dot(n, a - centre);
    float pb = glm::dot(n, b - centre);
    float pc = glm::dot(n, c - centre);
    float triMin = std::min({pa,pb,pc});
    float triMax = std::max({pa,pb,pc});
    float r = projectAABB(n, half);
    if (triMin > r || triMax < -r) return false;
    float overlap = std::min(r - triMin, triMax + r);
    if (overlap < depth) { depth = overlap; mtvAxis = n; }
    return true;
}

bool PlayerController::aabbTriTest(glm::vec3 aabbMin, glm::vec3 aabbMax,
                                    glm::vec3 a, glm::vec3 b, glm::vec3 c,
                                    glm::vec3& outMTV) const {
    glm::vec3 half   = (aabbMax - aabbMin) * 0.5f;
    glm::vec3 centre = (aabbMin + aabbMax) * 0.5f;
    glm::vec3 ab = b-a, bc = c-b, ca = a-c;
    glm::vec3 triNormal = glm::cross(ab, c-a);

    float depth = 1e9f;
    glm::vec3 mtvAxis{0,1,0};

    glm::vec3 axes[] = {{1,0,0},{0,1,0},{0,0,1}};
    for (auto& ax : axes)
        if (!axisTest(ax, centre, half, a, b, c, depth, mtvAxis)) return false;
    if (!axisTest(triNormal, centre, half, a, b, c, depth, mtvAxis)) return false;
    glm::vec3 edges[] = {ab, bc, ca};
    for (auto& e : edges)
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
    reg.emplace<CVelocity>(_player);
    reg.emplace<CAABB>(_player);
    reg.emplace<CGrounded>(_player);
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

        // ±1 = 27 chunks — sub-stepping handles tunnelling, no need for ±2
        for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) {
            ChunkCoord cc{cx+dx, cy+dy, cz+dz};
            auto it = _triSoups.find(cc);
            if (it == _triSoups.end()) continue;

            const auto& tris = it->second.tris;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                glm::vec3 mtv;
                if (!aabbTriTest(mn, mx, tris[i], tris[i+1], tris[i+2], mtv))
                    continue;
                tf.pos += mtv;
                mn = tf.pos - half;
                mx = tf.pos + half;
                float vDot = glm::dot(vel.vel, glm::normalize(mtv));
                if (vDot < 0) vel.vel -= glm::normalize(mtv) * vDot;
                if (glm::normalize(mtv).y > 0.5f) grounded.grounded = true;
            }
        }
    }
}

static glm::vec3 accelerate(glm::vec3 vel, glm::vec3 wishDir, float wishSpeed,
                              float accel, float dt) {
    float currentSpeed = glm::dot(vel, wishDir);
    float addSpeed     = wishSpeed - currentSpeed;
    if (addSpeed <= 0.f) return vel;
    float accelSpeed = std::min(accel * wishSpeed * dt, addSpeed);
    return vel + wishDir * accelSpeed;
}

void PlayerController::update(float dt, const Input& input) {
    // ── Spawn gate ────────────────────────────────────────────────────────────
    if (!_spawned) {
        if ((int)_triSoups.size() >= _chunksNeeded) {
            if (_hasPendingSpawn) {
                _reg.get<CTransform>(_player).pos = _pendingSpawn;
                _hasPendingSpawn = false;
            }
            _spawned = true;
        } else {
            return;
        }
    }

    auto& tf  = _reg.get<CTransform>(_player);
    auto& vel = _reg.get<CVelocity> (_player);
    auto& box = _reg.get<CAABB>     (_player);
    auto& gr  = _reg.get<CGrounded> (_player);

    // ── Mouse look ────────────────────────────────────────────────────────────
    _cam.applyMouse(input.mouseDelta());

    // ── Wish direction ────────────────────────────────────────────────────────
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

    bool  sprinting = input.key(GLFW_KEY_LEFT_SHIFT);
    float wishSpeed = (wishLen > 0.001f)
        ? Config::WALK_SPEED * (sprinting ? Config::SPRINT_MULT : 1.f)
        : 0.f;

    // ── Split velocity ────────────────────────────────────────────────────────
    glm::vec3 hVel{vel.vel.x, 0.f, vel.vel.z};
    float     yVel = vel.vel.y;

    if (gr.grounded) {
        float speed = glm::length(hVel);
        if (speed > 0.001f) {
            float drop     = speed * Config::FRICTION * dt;
            float newSpeed = std::max(speed - drop, 0.f);
            hVel *= newSpeed / speed;
        }
        hVel = accelerate(hVel, wishDir, wishSpeed, Config::GROUND_ACCEL, dt);
        if (yVel < 0.f) yVel = 0.f;
        if (input.key(GLFW_KEY_SPACE)) {
            yVel = Config::JUMP_VEL;
            gr.grounded = false;
        }
    } else {
        hVel = accelerate(hVel, wishDir, wishSpeed, Config::AIR_ACCEL, dt);
        yVel += Config::GRAVITY * dt;
    }

    vel.vel = {hVel.x, yVel, hVel.z};

    // ── Sub-stepped integration ───────────────────────────────────────────────
    const int   STEPS = 4;
    const float subDt = dt / STEPS;
    for (int s = 0; s < STEPS; s++) {
        tf.pos += vel.vel * subDt;
        resolveCollision(tf, vel, box, gr);
    }

    // ── Sync camera ───────────────────────────────────────────────────────────
    _cam.position = tf.pos + glm::vec3{0.f, box.half.y * 0.85f, 0.f};
}
